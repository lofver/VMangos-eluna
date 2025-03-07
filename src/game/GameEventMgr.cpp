/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "GameEventMgr.h"
#include "World.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "Creature.h"
#include "Object.h"
#include "PoolManager.h"
#include "ProgressBar.h"
#include "Language.h"
#include "Log.h"
#include "MapManager.h"
#include "MassMailMgr.h"
#include "SpellMgr.h"
#include "Policies/SingletonImp.h"

#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */
INSTANTIATE_SINGLETON_1(GameEventMgr);

bool GameEventMgr::CheckOneGameEvent(uint16 entry, time_t currenttime) const
{
    // Get the event information
    return mGameEvent[entry].start <= currenttime && currenttime < mGameEvent[entry].end &&
            (currenttime - mGameEvent[entry].start - (mGameEvent[entry].leapDays * DAY)) % (mGameEvent[entry].occurence * MINUTE) < mGameEvent[entry].length * MINUTE;
}

uint32 GameEventMgr::NextCheck(uint16 entry) const
{
    time_t currenttime = time(nullptr);

    // outdated event: we return max
    if (currenttime > mGameEvent[entry].end)
        return max_ge_check_delay;

    // never started event, we return delay before start
    if (mGameEvent[entry].start > currenttime)
        return uint32(mGameEvent[entry].start - currenttime);

    uint32 delay;
    // in event, we return the end of it
    if ((((currenttime - mGameEvent[entry].start - (mGameEvent[entry].leapDays * DAY)) % (mGameEvent[entry].occurence * 60)) < (mGameEvent[entry].length * 60)))
        // we return the delay before it ends
        delay = (mGameEvent[entry].length * MINUTE) - ((currenttime - mGameEvent[entry].start - (mGameEvent[entry].leapDays * DAY)) % (mGameEvent[entry].occurence * MINUTE));
    else                                                    // not in window, we return the delay before next start
        delay = (mGameEvent[entry].occurence * MINUTE) - ((currenttime - mGameEvent[entry].start - (mGameEvent[entry].leapDays * DAY)) % (mGameEvent[entry].occurence * MINUTE));
    // In case the end is before next check
    if (mGameEvent[entry].end  < time_t(currenttime + delay))
        return uint32(mGameEvent[entry].end - currenttime);

    return delay;
}

void GameEventMgr::StartEvent(uint16 event_id, bool overwrite /*=false*/, bool resume /*=false*/)
{
    if (!IsValidEvent(event_id))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "GameEventMgr::StartEvent game event id (%u) not exist in `game_event`.", event_id);
        return;
    }
    ApplyNewEvent(event_id, resume);
    
    //invoke enable on hardcoded events
    if (mGameEvent[event_id].hardcoded && !mGameEvent[event_id].disabled)
    {
        auto it = std::find_if(mGameEventHardcodedList.begin(), mGameEventHardcodedList.end(), [&](WorldEvent const* w) { return event_id == w->m_eventId; });
        if (mGameEventHardcodedList.end() != it)
        {
            (*it)->Enable();
        }
    }

    if (overwrite)
    {
        mGameEvent[event_id].start = time(nullptr);
        if (mGameEvent[event_id].end <= mGameEvent[event_id].start)
            mGameEvent[event_id].end = mGameEvent[event_id].start + mGameEvent[event_id].length;
    }
#ifdef ENABLE_ELUNA
    if (IsActiveEvent(event_id))
        sEluna->OnGameEventStart(event_id);
#endif /* ENABLE_ELUNA */
}

void GameEventMgr::StopEvent(uint16 event_id, bool overwrite)
{
    if (!IsValidEvent(event_id))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "GameEventMgr::StopEvent game event id (%u) not exist in `game_event`.", event_id);
        return;
    }
    UnApplyEvent(event_id);
    if (overwrite)
    {
        mGameEvent[event_id].start = time(nullptr) - mGameEvent[event_id].length * MINUTE;
        if (mGameEvent[event_id].end <= mGameEvent[event_id].start)
            mGameEvent[event_id].end = mGameEvent[event_id].start + mGameEvent[event_id].length;
    }
#ifdef ENABLE_ELUNA
    if (!IsActiveEvent(event_id))
        sEluna->OnGameEventStop(event_id);
#endif /* ENABLE_ELUNA */
}

void GameEventMgr::EnableEvent(uint16 event_id, bool enable)
{
    // skip if event not exists or length <= 0
    if (!IsValidEvent(event_id))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "GameEventMgr::EnableEvent game event id (%u) not exist in `game_event`.", event_id);
        return;
    }

    uint8 disabled = enable ? 0 : 1;

    // skip if event is already in desired state
    if (mGameEvent[event_id].disabled == disabled)
        return;

    // change state
    mGameEvent[event_id].disabled = disabled;
    WorldDatabase.PExecute("UPDATE `game_event` SET `disabled` = '%u' WHERE `entry` = '%u'", disabled, event_id);
   
    // we take no action if event needs to be started: GameEvent system will start it for us on its next iteration
    if (!IsActiveEvent(event_id))
        return;

    // disabled event should be stopped also, thus we do it here both for regular and hardcoded events
    auto it = std::find_if(mGameEventHardcodedList.begin(), mGameEventHardcodedList.end(), [&](WorldEvent const* w) { return event_id == w->m_eventId; });

    if (mGameEventHardcodedList.end() != it)
    {
        if (!enable)
            (*it)->Disable();
        else
            (*it)->Enable();
    }
    else
    {
        StopEvent(event_id, true);
    }
}

bool GameEventMgr::IsEnabled(uint16 event_id)
{
    if (!IsValidEvent(event_id))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "GameEventMgr::IsEnabled game event id (%u) not exist in `game_event`.", event_id);
        return false;
    }

    return mGameEvent[event_id].disabled == 0;
}

void GameEventMgr::LoadFromDB()
{
    {
        QueryResult* result = WorldDatabase.Query("SELECT MAX(entry) FROM game_event");
        if (!result)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Table game_event is empty.");
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
            return;
        }

        Field* fields = result->Fetch();

        uint32 max_event_id = fields[0].GetUInt16();
        delete result;

        mGameEvent.resize(max_event_id + 1);
    }

    QueryResult* result = WorldDatabase.Query("SELECT entry,UNIX_TIMESTAMP(start_time),UNIX_TIMESTAMP(end_time),occurence,length,holiday,description,hardcoded,disabled,patch_min,patch_max FROM game_event");
    if (!result)
    {
        mGameEvent.clear();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Table game_event is empty!");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        return;
    }

    uint32 count = 0;

    {
        BarGoLink bar(result->GetRowCount());
        do
        {
            ++count;
            Field* fields = result->Fetch();

            bar.step();

            uint16 event_id = fields[0].GetUInt16();
            if (event_id == 0)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `game_event` game event id (%i) is reserved and can't be used.", event_id);
                continue;
            }

            GameEventData& pGameEvent = mGameEvent[event_id];
            uint64 starttime        = fields[1].GetUInt64();
            pGameEvent.start        = time_t(starttime);
            uint64 endtime          = fields[2].GetUInt64();
            pGameEvent.end          = time_t(endtime);
            pGameEvent.occurence    = fields[3].GetUInt32();
            pGameEvent.length       = fields[4].GetUInt32();
            pGameEvent.holiday_id   = HolidayIds(fields[5].GetUInt32());

            if (pGameEvent.length == 0)                         // length>0 is validity check
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `game_event` game event id (%i) have length 0 and can't be used.", event_id);
                continue;
            }

            pGameEvent.description  = fields[6].GetCppString();
            pGameEvent.hardcoded    = fields[7].GetUInt8();
            pGameEvent.disabled     = fields[8].GetUInt8();
            uint8 patch_min         = fields[9].GetUInt8();
            uint8 patch_max         = fields[10].GetUInt8();

            if ((patch_min > patch_max) || (patch_max > 10))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `game_event` game event id (%i) has invalid values patch_min=%u, patch_max=%u.", event_id, patch_min, patch_max);
                sLog.Out(LOG_DBERRFIX, LOG_LVL_MINIMAL, "UPDATE game_event SET patch_min=0, patch_max=10 WHERE entry=%u;", event_id);
                patch_min = 0;
                patch_max = 10;
            }

            if (!((sWorld.GetWowPatch() >= patch_min) && (sWorld.GetWowPatch() <= patch_max)))
                pGameEvent.disabled = 1;

            // Leap days are needed to adjust yearly events
            if (pGameEvent.occurence == default_year_length && pGameEvent.length < default_year_length)
            {
                time_t current = time(0);
                tm tm_start = *gmtime(&pGameEvent.start);
                tm tm_current = *gmtime(&current);

                if (tm_current.tm_year > tm_start.tm_year)
                    for (int i = tm_start.tm_year; i < tm_current.tm_year; i++)
                        if (isLeapYear(i + 1900))
                            pGameEvent.leapDays++;
            }
        }
        while (result->NextRow());
        delete result;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u game events", count);
    }

    // initialize hardcoded events
    LoadHardcodedEvents(mGameEventHardcodedList);

    std::map<uint16, int16> pool2event;                     // for check unique spawn event associated with pool
    std::map<uint32, int16> creature2event;                 // for check unique spawn event associated with creature
    std::map<uint32, int16> go2event;                       // for check unique spawn event associated with gameobject

    // list only positive event top pools, filled at creature/gameobject loading
    mGameEventSpawnPoolIds.resize(mGameEvent.size());

    mGameEventCreatureGuids.resize(mGameEvent.size() * 2 - 1);
    //                                   1              2
    result = WorldDatabase.Query("SELECT creature.guid, game_event_creature.event "
                                 "FROM creature JOIN game_event_creature ON creature.guid = game_event_creature.guid");

    count = 0;
    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u creatures in game events", count);
    }
    else
    {

        BarGoLink bar(result->GetRowCount());
        do
        {
            Field* fields = result->Fetch();

            bar.step();

            uint32 guid    = fields[0].GetUInt32();
            int16 event_id = fields[1].GetInt16();

            if (event_id == 0)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_creature` game event id (%i) not allowed", event_id);
                continue;
            }

            if (!IsValidEvent(std::abs(event_id)))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_creature` game event id (%i) not exist in `game_event`", event_id);
                continue;
            }

            if (!sObjectMgr.IsExistingCreatureGuid(guid))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_creature` game event id (%i) contains non-existent creature guid (%u)", event_id, guid);
                continue;
            }

            int32 internal_event_id = mGameEvent.size() + event_id - 1;

            ++count;

            // spawn objects at event can be grouped in pools and then affected pools have stricter requirements for this case
            if (event_id > 0)
            {
                creature2event[guid] = event_id;

                // not list explicitly creatures from pools in event creature list
                if (uint16 topPoolId =  sPoolMgr.IsPartOfTopPool<Creature>(guid))
                {
                    int16& eventRef = pool2event[topPoolId];
                    if (eventRef != 0)
                    {
                        if (eventRef != event_id)
                            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "`game_event_creature` have creature (GUID: %u) for event %i from pool or subpool of pool (ID: %u) but pool have already content from event %i. Pool don't must have content for different events!", guid, event_id, topPoolId, eventRef);
                    }
                    else
                    {
                        eventRef = event_id;
                        mGameEventSpawnPoolIds[event_id].push_back(topPoolId);
                        sPoolMgr.RemoveAutoSpawnForPool(topPoolId);
                    }

                    continue;
                }
            }

            GuidList& crelist = mGameEventCreatureGuids[internal_event_id];
            crelist.push_back(guid);

        }
        while (result->NextRow());
        delete result;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u creatures in game events", count);
    }

    mGameEventGameobjectGuids.resize(mGameEvent.size() * 2 - 1);
    //                                   1                2
    result = WorldDatabase.Query("SELECT gameobject.guid, game_event_gameobject.event "
                                 "FROM gameobject JOIN game_event_gameobject ON gameobject.guid=game_event_gameobject.guid");

    count = 0;
    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u gameobjects in game events", count);
    }
    else
    {

        BarGoLink bar(result->GetRowCount());
        do
        {
            Field* fields = result->Fetch();

            bar.step();

            uint32 guid    = fields[0].GetUInt32();
            int16 event_id = fields[1].GetInt16();

            if (event_id == 0)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_gameobject` game event id (%i) not allowed", event_id);
                continue;
            }

            if (!IsValidEvent(std::abs(event_id)))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_gameobject` game event id (%i) not exist in `game_event`", event_id);
                continue;
            }

            int32 internal_event_id = mGameEvent.size() + event_id - 1;

            ++count;

            // spawn objects at event can be grouped in pools and then affected pools have stricter requirements for this case
            if (event_id > 0)
            {
                go2event[guid] = event_id;

                // not list explicitly gameobjects from pools in event gameobject list
                if (uint16 topPoolId =  sPoolMgr.IsPartOfTopPool<GameObject>(guid))
                {
                    int16& eventRef = pool2event[topPoolId];
                    if (eventRef != 0)
                    {
                        if (eventRef != event_id)
                            sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_gameobject` have gameobject (GUID: %u) for event %i from pool or subpool of pool (ID: %u) but pool have already content from event %i. Pool don't must have content for different events!", guid, event_id, topPoolId, eventRef);
                    }
                    else
                    {
                        eventRef = event_id;
                        mGameEventSpawnPoolIds[event_id].push_back(topPoolId);
                        sPoolMgr.RemoveAutoSpawnForPool(topPoolId);
                    }

                    continue;
                }
            }

            GuidList& golist = mGameEventGameobjectGuids[internal_event_id];
            golist.push_back(guid);

        }
        while (result->NextRow());
        delete result;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u gameobjects in game events", count);
    }

    // now recheck that all eventPools linked with events after our skip pools with parents
    for (const auto& itr : pool2event)
    {
        uint16 pool_id = itr.first;
        int16 event_id = itr.second;

        sPoolMgr.CheckEventLinkAndReport(pool_id, event_id, creature2event, go2event);
    }

    mGameEventCreatureData.resize(mGameEvent.size());
    //                                     0       1        2             3               4           5              6
    result = WorldDatabase.PQuery("SELECT `guid`, `event`, `display_id`, `equipment_id`, `entry_id`, `spell_start`, `spell_end` "
                                  "FROM `game_event_creature_data` t1 WHERE `patch`=(SELECT max(`patch`) FROM `game_event_creature_data` t2 WHERE t1.`guid`=t2.`guid` && t1.`event`=t2.`event` && `patch` <= %u)", sWorld.GetWowPatch());

    count = 0;
    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u creature reactions at game events", count);
    }
    else
    {

        BarGoLink bar(result->GetRowCount());
        do
        {
            Field* fields = result->Fetch();

            bar.step();
            uint32 guid     = fields[0].GetUInt32();
            uint16 event_id = fields[1].GetUInt16();

            if (event_id == 0)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_creature_data` game event id (%i) is reserved and can't be used." , event_id);
                continue;
            }

            if (!IsValidEvent(event_id))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_creature_data` game event id (%u) not exist in `game_event`", event_id);
                continue;
            }

            if (!sObjectMgr.IsExistingCreatureGuid(guid))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_creature_data` game event id (%u) contains non-existent creature guid (%u)", event_id, guid);
                continue;
            }

            ++count;
            GameEventCreatureDataList& equiplist = mGameEventCreatureData[event_id];
            GameEventCreatureData newData;
            newData.display_id = fields[2].GetUInt32();
            newData.equipment_id = fields[3].GetUInt32();
            newData.entry_id = fields[4].GetUInt32();
            newData.spell_id_start = fields[5].GetUInt32();
            newData.spell_id_end = fields[6].GetUInt32();

            if (newData.equipment_id && !sObjectMgr.GetEquipmentInfo(newData.equipment_id))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `game_event_creature_data` have creature (Guid: %u) with equipment_id %u not found in table `creature_equip_template`, set to no equipment.", guid, newData.equipment_id);
                newData.equipment_id = 0;
            }

            if (newData.entry_id && !ObjectMgr::GetCreatureTemplate(newData.entry_id))
            {
                if (!sObjectMgr.IsExistingCreatureId(newData.entry_id))
                    sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `game_event_creature_data` have creature (Guid: %u) with event time entry %u not found in table `creature_template`, set to no 0.", guid, newData.entry_id);
                newData.entry_id = 0;
            }

            if (newData.spell_id_start && !sSpellMgr.GetSpellEntry(newData.spell_id_start))
            {
                if (!sSpellMgr.IsExistingSpellId(newData.spell_id_start))
                    sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `game_event_creature_data` have creature (Guid: %u) with nonexistent spell_start %u, set to no start spell.", guid, newData.spell_id_start);
                newData.spell_id_start = 0;
            }

            if (newData.spell_id_end && !sSpellMgr.GetSpellEntry(newData.spell_id_end))
            {
                if (!sSpellMgr.IsExistingSpellId(newData.spell_id_end))
                    sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `game_event_creature_data` have creature (Guid: %u) with nonexistent spell_end %u, set to no end spell.", guid, newData.spell_id_end);
                newData.spell_id_end = 0;
            }

            equiplist.push_back(GameEventCreatureDataPair(guid, newData));
            mGameEventCreatureDataPerGuid.insert(GameEventCreatureDataPerGuidMap::value_type(guid, event_id));

        }
        while (result->NextRow());
        delete result;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u creature reactions at game events", count);
    }

    mGameEventQuests.resize(mGameEvent.size());

    result = WorldDatabase.PQuery("SELECT quest, event FROM game_event_quest WHERE patch_min <= %u", sWorld.GetWowPatch());

    count = 0;
    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u quests additions in game events", count);
    }
    else
    {

        BarGoLink bar(result->GetRowCount());
        do
        {
            Field* fields = result->Fetch();

            bar.step();
            uint32 quest    = fields[0].GetUInt32();
            uint16 event_id = fields[1].GetUInt16();

            if (event_id == 0)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_quest` game event id (%i) is reserved and can't be used.", event_id);
                continue;
            }

            if (!IsValidEvent(event_id))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_quest` game event id (%u) not exist in `game_event`", event_id);
                continue;
            }

            Quest const* pQuest = sObjectMgr.GetQuestTemplate(quest);

            if (!pQuest)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "Table `game_event_quest` contain entry for quest %u (event %u) but this quest does not exist. Skipping.", quest, event_id);
                continue;
            }

            // disable any event specific quest (for cases where creature is spawned, but event not active).
            const_cast<Quest*>(pQuest)->SetQuestActiveState(false);

            ++count;

            QuestList& questlist = mGameEventQuests[event_id];
            questlist.push_back(quest);

        }
        while (result->NextRow());
        delete result;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u quest additions in game events", count);
    }

    mGameEventMails.resize(mGameEvent.size() * 2 - 1);

    result = WorldDatabase.Query("SELECT event, raceMask, quest, mailTemplateId, senderEntry FROM game_event_mail");

    count = 0;
    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u start/end game event mails", count);
    }
    else
    {

        BarGoLink bar(result->GetRowCount());
        do
        {
            Field* fields = result->Fetch();

            bar.step();
            uint16 event_id = fields[0].GetUInt16();

            GameEventMail mail;
            mail.raceMask       = fields[1].GetUInt32();
            mail.questId        = fields[2].GetUInt32();
            mail.mailTemplateId = fields[3].GetUInt32();
            mail.senderEntry    = fields[4].GetUInt32();

            if (event_id == 0)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_mail` game event id (%i) not allowed", event_id);
                continue;
            }

            if (!IsValidEvent(event_id))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "`game_event_mail` game event id (%u) not exist in `game_event`", event_id);
                continue;
            }

            int32 internal_event_id = mGameEvent.size() + event_id - 1;

            if (!(mail.raceMask & RACEMASK_ALL_PLAYABLE))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "Table `game_event_mail` have raceMask (%u) requirement for game event %i that not include any player races, ignoring.", mail.raceMask, event_id);
                continue;
            }

            if (mail.questId && !sObjectMgr.GetQuestTemplate(mail.questId))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "Table `game_event_mail` have nonexistent quest (%u) requirement for game event %i, ignoring.", mail.questId, event_id);
                continue;
            }

            if (!sMailTemplateStorage.LookupEntry<MailTemplateEntry>(mail.mailTemplateId))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "Table `game_event_mail` have invalid mailTemplateId (%u) for game event %i that invalid not include any player races, ignoring.", mail.mailTemplateId, event_id);
                continue;
            }

            if (!ObjectMgr::GetCreatureTemplate(mail.senderEntry))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "Table `game_event_mail` have nonexistent sender creature entry (%u) for game event %i that invalid not include any player races, ignoring.", mail.senderEntry, event_id);
                continue;
            }

            ++count;

            MailList& maillist = mGameEventMails[internal_event_id];
            maillist.push_back(mail);

        }
        while (result->NextRow());
        delete result;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u start/end game event mails", count);
    }
}

uint32 GameEventMgr::Initialize()                           // return the next event delay in ms
{
    m_ActiveEvents.clear();

    ActiveEvents activeAtShutdown;

    if (QueryResult* result = CharacterDatabase.Query("SELECT event FROM game_event_status"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint16 event_id = fields[0].GetUInt16();
            activeAtShutdown.insert(event_id);
        }
        while (result->NextRow());
        delete result;

        CharacterDatabase.Execute("TRUNCATE game_event_status");
    }

    uint32 delay = Update(&activeAtShutdown);
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "Game Event system initialized.");
    m_IsGameEventsInit = true;
    return delay;
}

void GameEventMgr::Initialize(MapPersistentState* state)
{
    // At map persistent state creating need only apply pool spawn modifications
    // other data is global and will be auto-apply
    for (const auto i : m_ActiveEvents)
        for (auto pool_itr = mGameEventSpawnPoolIds[i].begin(); pool_itr != mGameEventSpawnPoolIds[i].end(); ++pool_itr)
            sPoolMgr.InitSpawnPool(*state, *pool_itr);
}

// return the next event delay in ms
uint32 GameEventMgr::Update(ActiveEvents const* activeAtShutdown /*= nullptr*/)
{
    // process hardcoded events
    time_t currenttime = time(nullptr);
    uint32 nextEventDelay = max_ge_check_delay;             // 1 day

    for (const auto& hEvent_iter : mGameEventHardcodedList)
    {
        if (!mGameEvent[hEvent_iter->m_eventId].disabled)
        {
            hEvent_iter->Update();
            uint32 calcDelay = hEvent_iter->GetNextUpdateDelay();
            if (calcDelay < nextEventDelay)
                nextEventDelay = calcDelay;
        }
    }

    for (uint16 itr = 1; itr < mGameEvent.size(); ++itr)
    {
        // ignore hardcoded and disabled events
        if (mGameEvent[itr].hardcoded || mGameEvent[itr].disabled) continue;

        //sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Checking event %u",itr);
        if (CheckOneGameEvent(itr, currenttime))
        {
            //sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "GameEvent %u is active",itr->first);
            if (!IsActiveEvent(itr))
            {
                bool resume = activeAtShutdown && activeAtShutdown->find(itr) != activeAtShutdown->end();
                StartEvent(itr, false, resume);
            }
        }
        else
        {
            //sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "GameEvent %u is not active",itr->first);
            if (IsActiveEvent(itr))
                StopEvent(itr);
            else
            {
                if (!m_IsGameEventsInit)
                {
                    // spawn all negative ones for this event
                    GameEventSpawn(-itr);
                }
            }
        }

        uint32 calcDelay = NextCheck(itr);
        if (calcDelay < nextEventDelay)
            nextEventDelay = calcDelay;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "Next game event check in %u seconds.", nextEventDelay + 1);

    return (nextEventDelay + 1) * IN_MILLISECONDS;           // Add 1 second to be sure event has started/stopped at next call
}

void GameEventMgr::UnApplyEvent(uint16 event_id)
{
    m_ActiveEvents.erase(event_id);
    CharacterDatabase.PExecute("DELETE FROM game_event_status WHERE event = %u", event_id);

    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "GameEvent %u \"%s\" removed.", event_id, mGameEvent[event_id].description.c_str());
    // un-spawn positive event tagged objects
    GameEventUnspawn(event_id);
    // spawn negative event tagget objects
    int16 event_nid = (-1) * event_id;
    GameEventSpawn(event_nid);
    // restore equipment or display id
    UpdateCreatureData(event_id, false);
    // Remove quests that are events only to non event npc
    UpdateEventQuests(event_id, false);
    SendEventMails(event_nid);
}

void GameEventMgr::ApplyNewEvent(uint16 event_id, bool resume)
{
    m_ActiveEvents.insert(event_id);
    CharacterDatabase.PExecute("INSERT IGNORE INTO game_event_status (event) VALUES (%u)", event_id);

    if (sWorld.getConfig(CONFIG_BOOL_EVENT_ANNOUNCE))
        sWorld.SendWorldText(LANG_EVENTMESSAGE, mGameEvent[event_id].description.c_str());

    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "GameEvent %u \"%s\" started.", event_id, mGameEvent[event_id].description.c_str());
    // spawn positive event tagget objects
    GameEventSpawn(event_id);
    // un-spawn negative event tagged objects
    int16 event_nid = (-1) * event_id;
    GameEventUnspawn(event_nid);
    // Change equipement or display id
    UpdateCreatureData(event_id, true);
    // Add quests that are events only to non event npc
    UpdateEventQuests(event_id, true);

    // Not send mails at game event startup, if game event just resume after server shutdown (has been active at server before shutdown)
    if (!resume)
        SendEventMails(event_id);
}

void GameEventMgr::GameEventSpawn(int16 event_id)
{
    int32 internal_event_id = mGameEvent.size() + event_id - 1;

    if (internal_event_id < 0 || (size_t)internal_event_id >= mGameEventCreatureGuids.size())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "GameEventMgr::GameEventSpawn attempt access to out of range mGameEventCreatureGuids element %i (size: " SIZEFMTD ")", internal_event_id, mGameEventCreatureGuids.size());
        return;
    }

    for (const auto& itr : mGameEventCreatureGuids[internal_event_id])
    {
        // Add to correct cell
        CreatureData const* data = sObjectMgr.GetCreatureData(itr);
        if (data)
        {
            // negative event id for pool element meaning allow be used in next pool spawn
            if (event_id < 0)
            {
                if (uint16 pool_id = sPoolMgr.IsPartOfAPool<Creature>(itr))
                {
                    // will have chance at next pool update
                    sPoolMgr.SetExcludeObject<Creature>(pool_id, itr, false);
                    sPoolMgr.UpdatePoolInMaps<Creature>(pool_id);
                    continue;
                }
            }

            sObjectMgr.AddCreatureToGrid(itr, data);

            Creature::SpawnInMaps(itr, data);
        }
    }

    if (internal_event_id < 0 || (size_t)internal_event_id >= mGameEventGameobjectGuids.size())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "GameEventMgr::GameEventSpawn attempt access to out of range mGameEventGameobjectGuids element %i (size: " SIZEFMTD ")", internal_event_id, mGameEventGameobjectGuids.size());
        return;
    }

    for (const auto& itr : mGameEventGameobjectGuids[internal_event_id])
    {
        // Add to correct cell
        GameObjectData const* data = sObjectMgr.GetGOData(itr);
        if (data)
        {
            // negative event id for pool element meaning allow be used in next pool spawn
            if (event_id < 0)
            {
                if (uint16 pool_id = sPoolMgr.IsPartOfAPool<GameObject>(itr))
                {
                    // will have chance at next pool update
                    sPoolMgr.SetExcludeObject<GameObject>(pool_id, itr, false);
                    sPoolMgr.UpdatePoolInMaps<GameObject>(pool_id);
                    continue;
                }
            }

            sObjectMgr.AddGameobjectToGrid(itr, data);

            GameObject::SpawnInMaps(itr, data);
        }
    }

    if (event_id > 0)
    {
        if ((size_t)event_id >= mGameEventSpawnPoolIds.size())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "GameEventMgr::GameEventSpawn attempt access to out of range mGameEventSpawnPoolIds element %i (size: " SIZEFMTD ")", event_id, mGameEventSpawnPoolIds.size());
            return;
        }

        for (const auto& itr : mGameEventSpawnPoolIds[event_id])
            sPoolMgr.SpawnPoolInMaps(itr, true);
    }
}

void GameEventMgr::GameEventUnspawn(int16 event_id)
{
    int32 internal_event_id = mGameEvent.size() + event_id - 1;

    if (internal_event_id < 0 || (size_t)internal_event_id >= mGameEventCreatureGuids.size())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "GameEventMgr::GameEventUnspawn attempt access to out of range mGameEventCreatureGuids element %i (size: " SIZEFMTD ")", internal_event_id, mGameEventCreatureGuids.size());
        return;
    }

    for (const auto& itr : mGameEventCreatureGuids[internal_event_id])
    {
        // Remove the creature from grid
        if (CreatureData const* data = sObjectMgr.GetCreatureData(itr))
        {
            // negative event id for pool element meaning unspawn in pool and exclude for next spawns
            if (event_id < 0)
            {
                if (uint16 poolid = sPoolMgr.IsPartOfAPool<Creature>(itr))
                {
                    sPoolMgr.SetExcludeObject<Creature>(poolid, itr, true);
                    sPoolMgr.UpdatePoolInMaps<Creature>(poolid, itr);
                    continue;
                }
            }

            // Remove spawn data
            sObjectMgr.RemoveCreatureFromGrid(itr, data);

            // Remove spawned cases
            Creature::AddToRemoveListInMaps(itr, data);
        }
    }

    if (internal_event_id < 0 || (size_t)internal_event_id >= mGameEventGameobjectGuids.size())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "GameEventMgr::GameEventUnspawn attempt access to out of range mGameEventGameobjectGuids element %i (size: " SIZEFMTD ")", internal_event_id, mGameEventGameobjectGuids.size());
        return;
    }

    for (const auto& itr : mGameEventGameobjectGuids[internal_event_id])
    {
        // Remove the gameobject from grid
        if (GameObjectData const* data = sObjectMgr.GetGOData(itr))
        {
            // negative event id for pool element meaning unspawn in pool and exclude for next spawns
            if (event_id < 0)
            {
                if (uint16 poolid = sPoolMgr.IsPartOfAPool<GameObject>(itr))
                {
                    sPoolMgr.SetExcludeObject<GameObject>(poolid, itr, true);
                    sPoolMgr.UpdatePoolInMaps<GameObject>(poolid, itr);
                    continue;
                }
            }

            // Remove spawn data
            sObjectMgr.RemoveGameobjectFromGrid(itr, data);

            // Remove spawned cases
            GameObject::AddToRemoveListInMaps(itr, data);
        }
    }

    if (event_id > 0)
    {
        if ((size_t)event_id >= mGameEventSpawnPoolIds.size())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "GameEventMgr::GameEventUnspawn attempt access to out of range mGameEventSpawnPoolIds element %i (size: " SIZEFMTD ")", event_id, mGameEventSpawnPoolIds.size());
            return;
        }

        for (const auto& itr : mGameEventSpawnPoolIds[event_id])
            sPoolMgr.DespawnPoolInMaps(itr);
    }
}

GameEventCreatureData const* GameEventMgr::GetCreatureUpdateDataForActiveEvent(uint32 lowguid) const
{
    // only for active event, creature can be listed for many so search all
    uint32 event_id = 0;
    auto bounds = mGameEventCreatureDataPerGuid.equal_range(lowguid);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
    {
        if (IsActiveEvent(itr->second))
        {
            event_id = itr->second;
            break;
        }
    }

    if (!event_id)
        return nullptr;

    for (const auto& itr : mGameEventCreatureData[event_id])
        if (itr.first == lowguid)
            return &itr.second;

    return nullptr;
}

struct GameEventUpdateCreatureDataInMapsWorker
{
    GameEventUpdateCreatureDataInMapsWorker(ObjectGuid guid, CreatureData const* data, GameEventCreatureData* event_data, bool activate)
        : i_guid(guid), i_data(data), i_event_data(event_data), i_activate(activate) {}

    void operator()(Map* map) const
    {
        if (Creature* pCreature = map->GetCreature(i_guid))
        {
            pCreature->UpdateEntry(pCreature->GetOriginalEntry(), i_activate ? i_event_data : nullptr);

            // spells not casted for event remove case (sent nullptr into update), do it
            if (!i_activate)
                pCreature->ApplyGameEventSpells(i_event_data, false);
        }
    }

    ObjectGuid i_guid;
    CreatureData const* i_data;
    GameEventCreatureData* i_event_data;
    bool i_activate;
};

void GameEventMgr::UpdateCreatureData(int16 event_id, bool activate)
{
    for (auto& itr : mGameEventCreatureData[event_id])
    {
        // Remove the creature from grid
        CreatureData const* data = sObjectMgr.GetCreatureData(itr.first);
        if (!data)
            continue;

        // Update if spawned
        GameEventUpdateCreatureDataInMapsWorker worker(data->GetObjectGuid(itr.first), data, &itr.second, activate);
        sMapMgr.DoForAllMapsWithMapId(data->position.mapId, worker);
    }
}

void GameEventMgr::UpdateEventQuests(uint16 event_id, bool Activate)
{
    for (const auto& itr : mGameEventQuests[event_id])
    {
        Quest const* pQuest = sObjectMgr.GetQuestTemplate(itr);

        //if (Activate)
        //{
        // TODO: implement way to reset quests when event begin.
        //}

        const_cast<Quest*>(pQuest)->SetQuestActiveState(Activate);
    }
}

void GameEventMgr::SendEventMails(int16 event_id)
{
    int32 internal_event_id = mGameEvent.size() + event_id - 1;

    MailList const& mails = mGameEventMails[internal_event_id];

    for (const auto& mail : mails)
    {
        if (mail.questId)
        {
            // need special query
            std::ostringstream ss;
            ss << "SELECT characters.guid FROM characters, character_queststatus "
               "WHERE (1 << (characters.race - 1)) & "
               << mail.raceMask
               << " AND characters.deleted_time IS NULL AND character_queststatus.guid = characters.guid AND character_queststatus.quest = "
               << mail.questId
               << " AND character_queststatus.rewarded <> 0";
            sMassMailMgr.AddMassMailTask(new MailDraft(mail.mailTemplateId), MailSender(MAIL_CREATURE, mail.senderEntry), ss.str().c_str());
        }
        else
            sMassMailMgr.AddMassMailTask(new MailDraft(mail.mailTemplateId), MailSender(MAIL_CREATURE, mail.senderEntry), mail.raceMask);
    }
}

// Get the Game Event ID for Creature by guid
template <>
int16 GameEventMgr::GetGameEventId<Creature>(uint32 guid_or_poolid)
{
    for (uint16 i = 0; i < mGameEventCreatureGuids.size(); i++) // 0 <= i <= 2*(S := mGameEvent.size()) - 2
        for (GuidList::const_iterator itr = mGameEventCreatureGuids[i].begin(); itr != mGameEventCreatureGuids[i].end(); ++itr)
            if (*itr == guid_or_poolid)
                return i + 1 - mGameEvent.size();       // -S *1 + 1 <= . <= 1*S - 1
    return 0;
}

// Get the Game Event ID for GameObject by guid
template <>
int16 GameEventMgr::GetGameEventId<GameObject>(uint32 guid_or_poolid)
{
    for (uint16 i = 0; i < mGameEventGameobjectGuids.size(); i++)
        for (GuidList::const_iterator itr = mGameEventGameobjectGuids[i].begin(); itr != mGameEventGameobjectGuids[i].end(); ++itr)
            if (*itr == guid_or_poolid)
                return i + 1 - mGameEvent.size();       // -S *1 + 1 <= . <= 1*S - 1
    return 0;
}

// Get the Game Event ID for Pool by pool ID
template <>
int16 GameEventMgr::GetGameEventId<Pool>(uint32 guid_or_poolid)
{
    for (uint16 i = 0; i < mGameEventSpawnPoolIds.size(); i++)
        for (IdList::const_iterator itr = mGameEventSpawnPoolIds[i].begin(); itr != mGameEventSpawnPoolIds[i].end(); ++itr)
            if (*itr == guid_or_poolid)
                return i;
    return 0;
}

GameEventMgr::GameEventMgr()
{
    m_IsGameEventsInit = false;
    m_IsSilithusEventCompleted = false;
}

bool GameEventMgr::IsActiveHoliday(HolidayIds id)
{
    if (id == HOLIDAY_NONE)
        return false;

    for (const auto i : m_ActiveEvents)
        if (mGameEvent[i].holiday_id == id)
            return true;

    return false;
}

/*
 * Silithus PvP
 */
bool GameEventMgr::GetSilithusPVPEventCompleted() const
{
    return m_IsSilithusEventCompleted;
}

void GameEventMgr::SetSilithusPVPEventCompleted(bool state)
{
    m_IsSilithusEventCompleted = state;
}

void GameEventMgr::UpdateSilithusPVP()
{
    SilithusPVPEventState event;
    time_t rawtime;
    time(&rawtime);

    struct tm *timeinfo;
    timeinfo = localtime(&rawtime);

    /** Event start every 6hours for 2hours */
    uint32 occurency = 6;
    if (timeinfo->tm_hour % occurency == 0 && timeinfo->tm_min == 0)
    {
        SetSilithusPVPEventCompleted(false);
        event = SILITHUS_PVP_EVENT_ON;
    }
    else if ((timeinfo->tm_hour % occurency == 0 || timeinfo->tm_hour % occurency == 1) && !GetSilithusPVPEventCompleted())
        event = SILITHUS_PVP_EVENT_ON;
    else
    {
        SetSilithusPVPEventCompleted(true);
        event = SILITHUS_PVP_EVENT_OFF;
    }

    if (event == SILITHUS_PVP_EVENT_ON)
    {
        if (!IsActiveEvent(SILITHUS_PVP_EVENT_ON))
        {
            sLog.Out(LOG_BG, LOG_LVL_DETAIL, "[SilithusPVPEvent] started %u", SILITHUS_PVP_EVENT_ON);
            StartEvent(SILITHUS_PVP_EVENT_ON);
            sWorld.SendGlobalText("Les collecteurs de Silithystes sont repares! Depechez vous de revenir en Silithus et reprenez le travail soldat!", nullptr);
        }
    }
    else if (IsActiveEvent(SILITHUS_PVP_EVENT_ON))
    {
        sLog.Out(LOG_BG, LOG_LVL_DETAIL, "[SilithusPVPEvent] stopped %u", SILITHUS_PVP_EVENT_ON);
        StopEvent(SILITHUS_PVP_EVENT_ON);
        sWorld.SendGlobalText("Le sable a enraille nos collecteurs de Silithystes, la collecte est interrompue en Silithus", nullptr);
    }
}
