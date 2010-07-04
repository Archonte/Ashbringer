/*
 * Copyright (C) 2005-2010 MaNGOS <http://getmangos.com/>
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

#include "Common.h"
#include "Log.h"
#include "Vehicle.h"
#include "ObjectMgr.h"
#include "Unit.h"
#include "Util.h"
#include "WorldPacket.h"
#include "TemporarySummon.h"
#include "CreatureAI.h"

VehicleKit::VehicleKit( Unit* base, VehicleEntry const* vehicleInfo ) : m_vehicleInfo(vehicleInfo), m_pBase(base), m_uiNumFreeSeats(0)
{
    for(uint32 i = 0; i < MAX_SEAT; ++i)
    {
        uint32 seatId = m_vehicleInfo->m_seatID[i];

        if(!seatId)
            continue;

        if(VehicleSeatEntry const *veSeat = sVehicleSeatStore.LookupEntry(seatId))
        {
            m_Seats.insert(std::make_pair(i, VehicleSeat(veSeat)));

            if (veSeat->IsUsable())
                ++m_uiNumFreeSeats;
        }
    }
}

VehicleKit::~VehicleKit()
{
}

void VehicleKit::RemoveAllPassengers()
{
    for(SeatMap::iterator itr = m_Seats.begin(); itr != m_Seats.end(); ++itr)
    {
        if(Unit *passenger = itr->second.passenger)
        {
            if(passenger->GetVehicleKit())
                passenger->GetVehicleKit()->RemoveAllPassengers();

            passenger->ExitVehicle();
        }
    }
}

bool VehicleKit::HasEmptySeat(int8 seatId) const
{
    SeatMap::const_iterator seat = m_Seats.find(seatId);
    if (seat == m_Seats.end()) return false;
    return !seat->second.passenger;
}

Unit *VehicleKit::GetPassenger(int8 seatId) const
{
    SeatMap::const_iterator seat = m_Seats.find(seatId);
    if (seat == m_Seats.end()) return NULL;
    return seat->second.passenger;
}

int8 VehicleKit::GetNextEmptySeat(int8 seatId, bool next) const
{
    SeatMap::const_iterator seat = m_Seats.find(seatId);
    if (seat == m_Seats.end()) return -1;
    while (seat->second.passenger || !seat->second.seatInfo->IsUsable())
    {
        if (next)
        {
            ++seat;
            if (seat == m_Seats.end())
                seat = m_Seats.begin();
        }
        else
        {
            if (seat == m_Seats.begin())
                seat = m_Seats.end();
            --seat;
        }
        if (seat->first == seatId)
            return -1; // no available seat
    }
    return seat->first;
}

bool VehicleKit::AddPassenger(Unit *unit, int8 seatId)
{
    if (unit->GetVehicle() != this)
        return false;

    SeatMap::iterator seat;
    if (seatId < 0) // no specific seat requirement
    {
        for (seat = m_Seats.begin(); seat != m_Seats.end(); ++seat)
            if (!seat->second.passenger && (seat->second.seatInfo->m_flags & SF_USABLE))
                break;

        if (seat == m_Seats.end()) // no available seat
            return false;
    }
    else
    {
        seat = m_Seats.find(seatId);
        if (seat == m_Seats.end())
            return false;

        if (seat->second.passenger)
            seat->second.passenger->ExitVehicle();
    }

    seat->second.passenger = unit;

    if (seat->second.seatInfo->IsUsable())
    {
        ASSERT(m_uiNumFreeSeats);
        --m_uiNumFreeSeats;

        if (!m_uiNumFreeSeats)
        {
            if (m_pBase->GetTypeId() == TYPEID_PLAYER)
                m_pBase->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_PLAYER_VEHICLE);
            else
                m_pBase->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPELLCLICK);
        }
    }

    unit->addUnitState(UNIT_STAT_ON_VEHICLE);

    VehicleSeatEntry const *veSeat = seat->second.seatInfo;
    float fsize = unit->GetFloatValue(OBJECT_FIELD_SCALE_X);

    unit->m_movementInfo.SetTransportData(m_pBase->GetGUID(),
        veSeat->m_attachmentOffsetX, veSeat->m_attachmentOffsetY, veSeat->m_attachmentOffsetZ,
        veSeat->m_passengerYaw, getMSTime(), seat->first, veSeat);

    unit->m_movementInfo.AddMovementFlag(MOVEFLAG_ONTRANSPORT);

    if(seat->second.seatInfo->m_flags & SF_MAIN_RIDER)
    {
        m_pBase->GetMotionMaster()->Clear(false);
        m_pBase->GetMotionMaster()->MoveIdle();
        m_pBase->SetCharmerGUID(unit->GetGUID());
        unit->SetCharmGUID(m_pBase->GetGUID());

        m_pBase->setFaction(unit->getFaction());

        if(CharmInfo* charmInfo = m_pBase->InitCharmInfo(m_pBase))
            charmInfo->InitVehicleCreateSpells();

        if(unit->GetTypeId() == TYPEID_PLAYER)
        {
            Player* pl = (Player*)unit;

            m_pBase->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);
            pl->SetClientControl(m_pBase, 1);
            pl->VehicleSpellInitialize();

            if(pl->GetGroup())
                pl->SetGroupUpdateFlag(GROUP_UPDATE_VEHICLE);
        }
    }

    if(unit->GetTypeId() == TYPEID_PLAYER)
    {
        Player* pl = (Player*)unit;
        pl->GetCamera().SetView(m_pBase);

        WorldPacket data(SMSG_FORCE_MOVE_ROOT, 10);
        data << unit->GetPackGUID();
        data << uint32((unit->m_movementInfo.GetVehicleSeatFlags() & SF_CAN_CAST) ? 2 : 0);
        unit->SendMessageToSet(&data, true);
    }

    unit->SendMonsterMoveTransport(m_pBase);

    if(m_pBase->GetTypeId() == TYPEID_UNIT)
    {
        RelocatePassengers(m_pBase->GetPositionX(), m_pBase->GetPositionY(), m_pBase->GetPositionZ(), m_pBase->GetOrientation());

        if (((Creature*)m_pBase)->AI())
            ((Creature*)m_pBase)->AI()->PassengerBoarded(unit, seat->first, true);
    }

    return true;
}

void VehicleKit::RemovePassenger(Unit *unit)
{
    if (unit->GetVehicle() != this)
        return;

    SeatMap::iterator seat;
    for(seat = m_Seats.begin(); seat != m_Seats.end(); ++seat)
    {
        if(seat->second.passenger == unit)
            break;
    }

    if(seat == m_Seats.end())
        return;

    seat->second.passenger = NULL;

    if (seat->second.seatInfo->IsUsable())
    {
        if (!m_uiNumFreeSeats)
        {
            if (m_pBase->GetTypeId() == TYPEID_PLAYER)
                m_pBase->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_PLAYER_VEHICLE);
            else
                m_pBase->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPELLCLICK);
        }

        ++m_uiNumFreeSeats;
    }

    unit->clearUnitState(UNIT_STAT_ON_VEHICLE);

    unit->m_movementInfo.ClearTransportData();
    unit->m_movementInfo.RemoveMovementFlag(MOVEFLAG_ONTRANSPORT);

    if(seat->second.seatInfo->m_flags & SF_MAIN_RIDER)
    {
        unit->RemoveSpellsCausingAura(SPELL_AURA_CONTROL_VEHICLE);
        m_pBase->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);
        unit->SetCharm(NULL);
        m_pBase->SetCharmerGUID(NULL);
        m_pBase->clearUnitState(UNIT_STAT_CONTROLLED);

        if(unit->GetTypeId() == TYPEID_PLAYER)
        {
            Player* pl = (Player*)unit;

            pl->SetClientControl(unit, 1);
            pl->RemovePetActionBar();

            if(pl->GetGroup())
                pl->SetGroupUpdateFlag(GROUP_UPDATE_VEHICLE);
        }
    }

    // restore player control
    if(unit->GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)unit)->GetCamera().ResetView();

        if(seat->second.seatInfo->m_flags & SF_CAN_CAST)
        {
            WorldPacket data(SMSG_FORCE_MOVE_UNROOT, 10);
            data << unit->GetPackGUID();
            data << uint32(2);                        // can rotate
            unit->SendMessageToSet(&data, true);
        }
        else
        {
            WorldPacket data(SMSG_FORCE_MOVE_UNROOT, 10);
            data << unit->GetPackGUID();
            data << uint32(0);                        // cannot rotate
            unit->SendMessageToSet(&data, true);
        }
    }

    if(m_pBase->GetTypeId() == TYPEID_UNIT)
    {
        if (((Creature*)m_pBase)->AI())
            ((Creature*)m_pBase)->AI()->PassengerBoarded(unit, seat->first, false);
    }
}

void VehicleKit::Install()
{
    if (m_pBase->GetTypeId() == TYPEID_UNIT)
    {
        Creature* base = (Creature*)m_pBase;

        if (m_vehicleInfo->m_powerType == POWER_STEAM)
        {
            base->setPowerType(POWER_ENERGY);
            base->SetMaxPower(POWER_ENERGY, 100);
            base->SetPower(POWER_ENERGY, 100);
        }
        else if (m_vehicleInfo->m_powerType == POWER_PYRITE)
        {
            base->setPowerType(POWER_ENERGY);
            base->SetMaxPower(POWER_ENERGY, 50);
            base->SetPower(POWER_ENERGY, 50);
        }
        else
        {
            for (uint32 i = 0; i < CREATURE_MAX_SPELLS; ++i)
            {
                if (!base->m_spells[i])
                    continue;

                SpellEntry const *spellInfo = sSpellStore.LookupEntry(base->m_spells[i]);
                if (!spellInfo)
                    continue;

                if (spellInfo->powerType == POWER_MANA)
                    break;

                if (spellInfo->powerType == POWER_ENERGY)
                {
                    base->setPowerType(POWER_ENERGY);
                    base->SetMaxPower(POWER_ENERGY, 100);
                    base->SetPower(POWER_ENERGY, 100);
                    break;
                }
            }
        }
    }

    Reset();
}

void VehicleKit::Die()
{
    for (SeatMap::iterator itr = m_Seats.begin(); itr != m_Seats.end(); ++itr)
    {
        if (Unit *passenger = itr->second.passenger)
        {
            if (passenger->GetTypeId() == TYPEID_UNIT && ((Creature*)passenger)->isTemporarySummon())
                passenger->setDeathState(JUST_DIED);
        }
    }
    RemoveAllPassengers();
}

void VehicleKit::Uninstall()
{
    for (SeatMap::iterator itr = m_Seats.begin(); itr != m_Seats.end(); ++itr)
    {
        if (Unit *passenger = itr->second.passenger)
        {
            if(passenger->GetTypeId() == TYPEID_UNIT && ((Creature*)passenger)->isTemporarySummon())
                ((TemporarySummon*)passenger)->UnSummon();
        }
    }
    RemoveAllPassengers();
}

void VehicleKit::Reset()
{
    if (m_pBase->GetTypeId() == TYPEID_PLAYER)
    {
        if (m_uiNumFreeSeats)
            m_pBase->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_PLAYER_VEHICLE);
    }
    else
    {
        InstallAllAccessories();
        if (m_uiNumFreeSeats)
            m_pBase->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPELLCLICK);
    }
}

void VehicleKit::InstallAllAccessories()
{
    VehicleAccessoryList const* mVehicleList = sObjectMgr.GetVehicleAccessoryList(m_pBase->GetEntry());
    if (!mVehicleList)
        return;

    for (VehicleAccessoryList::const_iterator itr = mVehicleList->begin(); itr != mVehicleList->end(); ++itr)
        InstallAccessory(itr->uiAccessory, itr->uiSeat, itr->bMinion);
}

void VehicleKit::InstallAccessory( uint32 entry, int8 seatId, bool minion /*= true*/ )
{
    if (Unit *passenger = GetPassenger(seatId))
    {
        // already installed
        if (passenger->GetEntry() == entry)
            return;

        passenger->ExitVehicle();
    }

    if (Creature *accessory = m_pBase->SummonCreature(entry, m_pBase->GetPositionX(), m_pBase->GetPositionY(), m_pBase->GetPositionZ(), 0.0f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30000))
        accessory->EnterVehicle(this, seatId);
}

void VehicleKit::RelocatePassengers(float x, float y, float z, float ang)
{
    for (SeatMap::const_iterator itr = m_Seats.begin(); itr != m_Seats.end(); ++itr)
    {
        if (Unit *passenger = itr->second.passenger)
        {
            float px = x + passenger->m_movementInfo.GetTransportPos()->x;
            float py = y + passenger->m_movementInfo.GetTransportPos()->y;
            float pz = z + passenger->m_movementInfo.GetTransportPos()->z;
            float po = ang + passenger->m_movementInfo.GetTransportPos()->o;

            passenger->SetPosition(px, py, pz, po);
        }
    }
}
