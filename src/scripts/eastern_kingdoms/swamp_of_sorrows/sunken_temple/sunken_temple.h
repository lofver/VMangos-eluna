/*
 * Copyright (C) 2006-2011 ScriptDev2 <http://www.scriptdev2.com/>
 * Copyright (C) 2010-2011 ScriptDev0 <http://github.com/mangos-zero/scriptdev0>
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

#pragma once

enum
{
    SUNKENTEMPLE_MAX_ENCOUNTER = 6,
    MAX_STATUES                = 6,

    // Don't change types 1,2 and 3 (handled in ACID)
    TYPE_ATALARION_OBSOLET= 1,
    TYPE_PROTECTORS_OBS   = 2,
    TYPE_JAMMALAN_OBS     = 3,

    TYPE_SECRET_CIRCLE    = 4,
    TYPE_PROTECTORS       = 5,
    TYPE_JAMMALAN         = 6,
    TYPE_MALFURION        = 7,
    TYPE_AVATAR           = 8,
    TYPE_ERANIKUS         = 9,

    NPC_ATALARION         = 8580,
    NPC_DREAMSCYTH        = 5721,
    NPC_WEAVER            = 5720,
    NPC_JAMMALAN          = 5710,
    NPC_AVATAR_OF_HAKKAR  = 8443,
    NPC_SHADE_OF_ERANIKUS = 5709,

    // Jammalain mini-bosses
    NPC_ZOLO              = 5712,
    NPC_GASHER            = 5713,
    NPC_LORO              = 5714,
    NPC_HUKKU             = 5715,
    NPC_ZULLOR            = 5716,
    NPC_MIJAN             = 5717,

    // Avatar of hakkar mobs
    NPC_SHADE_OF_HAKKAR   = 8440,  // Shade of Hakkar appears when the event starts; will despawn when avatar of hakkar is summoned
    NPC_BLOODKEEPER       = 8438,  // Spawned rarely and contains the hakkari blood -> used to extinguish the flames
    NPC_HAKKARI_MINION    = 8437,  // Npc randomly spawned during the event = trash
    NPC_SUPPRESSOR        = 8497,  // Npc summoned at one of the two doors and moves to the boss

    NPC_MALFURION         = 15362,
    AREATRIGGER_MALFURION = 4016,

    GO_IDOL_OF_HAKKAR     = 148838, // Appears when atalarion is summoned; this was removed in 4.0.1

    GO_ATALAI_STATUE_1    = 148830,
    GO_ATALAI_STATUE_2    = 148831,
    GO_ATALAI_STATUE_3    = 148832,
    GO_ATALAI_STATUE_4    = 148833,
    GO_ATALAI_STATUE_5    = 148834,
    GO_ATALAI_STATUE_6    = 148835,

    GO_ATALAI_LIGHT       = 148883, // Green light, activates when the correct statue is chosen
    GO_ATALAI_LIGHT_BIG   = 148937, // Big light, used at the altar event

    GO_ATALAI_TRAP_1      = 177484, // Trapps triggered if the wrong statue is activated
    GO_ATALAI_TRAP_2      = 177485, // The traps are spawned in DB randomly around the statues (we don't know exactly which statue has which trap)
    GO_ATALAI_TRAP_3      = 148837,

    GO_JAMMALAN_BARRIER   = 149431,

    SAY_JAMMALAN_INTRO    = 4490,
    SAY_DREAMSCYTHE_INTRO = 4364,
    SAY_DREAMSCYTHE_AGGRO = 6220,
    SAY_ATALALARION_AGGRO = 6216,
    SAY_ATALALARION_SPAWN = 4485,
};

