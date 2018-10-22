/*
 * SEGS - Super Entity Game Server
 * http://www.segs.io/
 * Copyright (c) 2006 - 2018 SEGS Team (see AUTHORS.md)
 * This software is licensed under the terms of the 3-clause BSD License. See LICENSE.md for details.
 */

/*!
 * @addtogroup GameData Projects/CoX/Common/GameData
 * @{
 */

#include "Character.h"

#include "CharacterHelpers.h"
#include "BitStream.h"
#include "Entity.h"
#include "Costume.h"
#include "Friend.h"
#include "serialization_common.h"
#include "Messages/GameDatabase/GameDBSyncEvents.h"
#include "GameData/chardata_serializers.h"
#include "GameData/entitydata_serializers.h"
#include "GameData/playerdata_definitions.h"
#include "GameData/playerdata_serializers.h"
#include "GameData/GameDataStore.h"
#include "Servers/MapServer/DataHelpers.h"
#include "Logging.h"
#include <QtCore/QString>
#include <QtCore/QDebug>
#include "Settings.h"

using namespace SEGSEvents;

Character::Character()
{
    m_multiple_costumes                         = false;
    m_current_costume_idx                       = 0;
    m_current_costume_set                       = false;
    m_char_data.m_supergroup_costume            = false;
    m_sg_costume                                = nullptr;
    m_char_data.m_using_sg_costume              = false;
    m_char_data.m_has_titles = m_char_data.m_has_the_prefix
            || !m_char_data.m_titles[0].isEmpty()
            || !m_char_data.m_titles[1].isEmpty()
            || !m_char_data.m_titles[2].isEmpty();
    m_char_data.m_sidekick.m_has_sidekick = false;
    m_char_data.m_current_attribs.initAttribArrays();

    int eh_idx = 0;
    for(CharacterEnhancement &eh : m_char_data.m_enhancements)
    {
        eh.m_slot_idx = eh_idx;
        ++eh_idx;
    }
}

void Character::reset()
{
    m_char_data.m_level = 0;
    m_char_data.m_combat_level = m_char_data.m_level;
    m_name="EMPTY";
    m_char_data.m_class_name="EMPTY";
    m_char_data.m_origin_name="EMPTY";
    m_villain=false;
    m_multiple_costumes=false;
    m_current_costume_idx=0;
    m_current_costume_set=false;
    m_char_data.m_supergroup_costume=false;
    m_sg_costume=nullptr;
    m_char_data.m_using_sg_costume=false;
    m_char_data.m_has_titles = false;
    m_char_data.m_sidekick.m_has_sidekick = false;
    m_char_data.m_powersets.clear();
}


bool Character::isEmpty()
{
    return ( 0==m_name.compare("EMPTY",Qt::CaseInsensitive)&&
            (0==m_char_data.m_class_name.compare("EMPTY",Qt::CaseInsensitive)));
}

void Character::setName(const QString &val )
{
    if(val.length()>0)
        m_name = val;
    else
        m_name = "EMPTY";
}

void Character::sendTray(BitStream &bs) const
{
    m_char_data.m_trays.serializeto(bs);
}

void Character::finalizeLevel()
{
    qCDebug(logPowers) << "Finalizing Level";

    GameDataStore &data(getGameData());
    uint32_t max_level = data.expMaxLevel();
    m_char_data.m_level = std::max(uint32_t(0), std::min(m_char_data.m_level, max_level));

    if(m_char_data.m_experience_points < data.expForLevel(m_char_data.m_level))
        m_char_data.m_experience_points = data.expForLevel(m_char_data.m_level);

    m_char_data.m_max_insp_rows = data.countForLevel(m_char_data.m_level, data.m_pi_schedule.m_InspirationRow);
    m_char_data.m_max_insp_cols = data.countForLevel(m_char_data.m_level, data.m_pi_schedule.m_InspirationCol);
    m_char_data.m_max_enhance_slots = data.countForLevel(m_char_data.m_level, data.m_pi_schedule.m_BoostSlot);

    if (m_char_data.m_sidekick.m_type  != SidekickType::IsSidekick){
         m_char_data.m_combat_level = m_char_data.m_level;              // if sidekicked, m_combat_level is linked to
         finalizeCombatLevel();
    }
    //if (m_char_data.m_sidekick.m_type   == SidekickType::IsMentor)       //todo: set sidekick's level to keep up with mentor

    m_char_data.m_security_threat = m_char_data.m_level;

    // client will only accept 5col x 4row of insps MAX
    assert(m_char_data.m_max_insp_cols <= 5 || m_char_data.m_max_insp_rows <= 4);

    // Add inherent powers for this level
    addPowersByLevel(QStringLiteral("Inherent"), QStringLiteral("Inherent"), m_char_data.m_level);

    /*
    int num_powersets = m_char_data.m_powersets.size();
    for(int idx = 1; idx < num_powersets; ++idx) // Skipping 0 powerset (temporary powers)
    {
        CharacterPowerSet pset = m_char_data.m_powersets[idx];
        for(CharacterPower &pow : pset.m_powers)
            reserveEnhancementSlot(&pow, m_char_data.m_level);
    }
    */

    m_char_data.m_has_updated_powers = true; // this must be true, because we're updating powers
    m_char_data.m_reset_powersets = true; // possible that we need to reset the powerset array client side

}

void Character::addStartingInspirations(QStringList &starting_insps)
{
    for (QString &name : starting_insps)
        addInspirationByName(m_char_data, name);
}

void Character::addStartingContact(const char *contact_name)
{
    Contact startingContact;
    startingContact.setName(contact_name); // "OfficerFlint
    startingContact.m_current_standing = 1;
    startingContact.m_notify_player = true;
    //startingContact.m_display_name = "Officer Flint";
    startingContact.m_task_index = 1;
    startingContact.m_npc_id = 1939; // Npc Id
    startingContact.m_has_location = true;
    //startingContact.m_handle = 1; // ?
    startingContact.m_location_description = "Outbreak";
    //startingContact.m_location_name = "Outbreak Starting";
    //startingContact.m_location_map_name = "City_00_01"; //folder name?
    startingContact.m_location.location.x = -62.0;
    startingContact.m_location.location.y = 0.0;
    startingContact.m_location.location.z = 182.0;
    startingContact.m_confidant_threshold = 3;
    startingContact.m_friend_threshold = 2;
    startingContact.m_complete_threshold = 4;
    startingContact.m_can_use_cell = false;

    qCDebug(logMapEvents) << "Character addStartingContact";
    addContact(m_char_data, startingContact);
}

void Character::addStartingPowers(const QString &pcat_name, const QString &pset_name, const QStringList &power_names)
{
    PowerPool_Info ppool;

    ppool.m_pcat_idx = getPowerCatByName(pcat_name);
    ppool.m_pset_idx = getPowerSetByName(pset_name, ppool.m_pcat_idx);

    addPowerSet(m_char_data, ppool); // add empty powerset

    for(const QString &name : power_names)
    {
        // Because we pull from settings.cfg, it's possible
        // that power_names is empty but still contains one
        // item with a value of "". So we break here.
        if(name.isEmpty())
            break;

        ppool.m_pow_idx = getPowerByName(name, ppool.m_pcat_idx, ppool.m_pset_idx);
        addPower(m_char_data, ppool);
    }
}

void Character::addPowersByLevel(const QString &pcat_name, const QString &pset_name, uint32_t level)
{
    PowerPool_Info ppool;
    ppool.m_pcat_idx = getPowerCatByName(pcat_name);
    ppool.m_pset_idx = getPowerSetByName(pset_name, ppool.m_pcat_idx);

    Parse_PowerSet pset = getGameData().get_powerset(ppool.m_pcat_idx, ppool.m_pset_idx);

    // Iterate through the availability of all powers in the set
    for(size_t i = 0; i < pset.Available.size(); ++i)
    {
        if(level < pset.Available.at(i))
            continue;
        // skip prestige powers
        if(pset.m_Powers.at(i).m_Name.contains("prestige_"))
            continue;

        ppool.m_pow_idx = uint32_t(i);
        addPower(m_char_data, ppool); // skips powers already owned
    }
}

void Character::getPowerFromBuildInfo(BitStream &src)
{
    for(int i = 0; i < 2; ++i)
    {
        PowerPool_Info ppinfo;
        ppinfo.serializefrom(src);

        addPower(m_char_data, ppinfo);
    }
}

void Character::GetCharBuildInfo(BitStream &src)
{
    src.GetString(m_char_data.m_class_name);
    src.GetString(m_char_data.m_origin_name);

    qInfo() << "Loading Starting Character Settings...";
    QSettings config(Settings::getSettingsPath(),QSettings::IniFormat,nullptr);

    config.beginGroup("StartingCharacter");
        QStringList inherent_and_preorders = config.value("inherent_powers","Brawl").toString().split(',');
        QStringList starting_temps = config.value("starting_temps","EMP_Glove").toString().split(',');
        QStringList starting_insps = config.value("starting_inspirations","Resurgence").toString().split(',');
        uint startlevel = config.value(QStringLiteral("starting_level"), "1").toUInt() -1; //convert from 1-50 to 0-49
        uint startinf = config.value(QStringLiteral("starting_inf"), "0").toUInt();
    config.endGroup();

    m_char_data.m_level = startlevel;
    m_char_data.m_influence = startinf;

    // Temporary Powers MUST come first (must be idx 0)
    addStartingPowers(QStringLiteral("Temporary_Powers"), QStringLiteral("Temporary_Powers"), starting_temps);
    addStartingPowers(QStringLiteral("Inherent"), QStringLiteral("Inherent"), inherent_and_preorders);
    getPowerFromBuildInfo(src);     // primary, secondary

    // Now that character is created. Finalize level and update hp and end
    finalizeLevel();
    setMaxHP(*this); // set max hp
    setMaxEnd(*this); // set max end

    // This must come after finalize
    addStartingInspirations(starting_insps);      // resurgence and phenomenal_luck


    //addStartingContact("Officer Flint");

    m_char_data.m_trays.serializefrom(src);
}

void Character::sendEnhancements(BitStream &bs) const
{
    bs.StorePackedBits(5, m_char_data.m_enhancements.size()); // count
    for(size_t i = 0; i < m_char_data.m_enhancements.size(); ++i)
    {
        bs.StorePackedBits(3, m_char_data.m_enhancements[i].m_slot_idx); // boost idx, maybe use m_enhancement_idx
        bs.StoreBits(1, m_char_data.m_enhancements[i].m_slot_used); // 1 set, 0 clear
        if(m_char_data.m_enhancements[i].m_slot_used)
        {
            m_char_data.m_enhancements[i].m_enhance_info.serializeto(bs);
            bs.StorePackedBits(5, m_char_data.m_enhancements[i].m_level); // boost idx
            bs.StorePackedBits(2, m_char_data.m_enhancements[i].m_num_combines); // boost idx
        }
    }
}

void Character::sendInspirations(BitStream &bs) const
{
    int max_cols = m_char_data.m_max_insp_cols;
    int max_rows = m_char_data.m_max_insp_rows;

    bs.StorePackedBits(3, max_cols); // count
    bs.StorePackedBits(3, max_rows); // count

    for(int col = 0; col < max_cols; ++col)
    {
        for(int row = 0; row < max_rows; ++row)
        {
            bs.StoreBits(1, m_char_data.m_inspirations.value(col, row).m_has_insp);

            if(m_char_data.m_inspirations.value(col, row).m_has_insp)
                m_char_data.m_inspirations.value(col, row).m_insp_info.serializeto(bs);
        }
    }
}

void Character::sendOwnedPowers(BitStream &bs) const
{
    bs.StorePackedBits(4, m_char_data.m_powersets.size()); // count
    for(const CharacterPowerSet &pset : m_char_data.m_powersets)
    {
        bs.StorePackedBits(5, pset.m_level_bought);
        bs.StorePackedBits(4, pset.m_powers.size());
        for(const CharacterPower &power : pset.m_powers)
        {
            power.m_power_info.serializeto(bs);
            bs.StorePackedBits(5, power.m_level_bought);
            bs.StoreFloat(power.getPowerTemplate().Range);

            if(power.m_total_eh_slots > power.m_enhancements.size())
                qCWarning(logPowers) << "sendOwnedPowers: Total EH Slots larger than vector!";

            bs.StorePackedBits(4, power.m_enhancements.size());
            for(const CharacterEnhancement &eh : power.m_enhancements)
            {
                bs.StoreBits(1, eh.m_slot_used); // slot has enhancement
                if(eh.m_slot_used)
                {
                    eh.m_enhance_info.serializeto(bs);
                    bs.StorePackedBits(5, eh.m_level);
                    bs.StorePackedBits(2, eh.m_num_combines);
                }
            }
        }
    }
}

void Character::SendCharBuildInfo(BitStream &bs) const
{
    Character c = *this;
    bs.StoreString(getClass(c));   // class name
    bs.StoreString(getOrigin(c));  // origin name
    bs.StorePackedBits(5, getLevel(c)); // plevel is level
    PUTDEBUG("SendCharBuildInfo after plevel");

    // Owned Powers
    sendOwnedPowers(bs);
    PUTDEBUG("SendCharBuildInfo after powers");

    // Inspirations
    sendInspirations(bs);
    PUTDEBUG("SendCharBuildInfo after inspirations");

    // Enhancements
    sendEnhancements(bs);
    PUTDEBUG("SendCharBuildInfo after boosts");
}

void Character::serializetoCharsel( BitStream &bs, const QString& entity_map_name )
{
    Character c = *this;
    bs.StorePackedBits(1,getLevel(c));
    bs.StoreString(getName());
    bs.StoreString(getClass(c));
    bs.StoreString(getOrigin(c));
    if(m_costumes.empty())
    {
        assert(m_name.compare("EMPTY")==0); // only empty characters can have no costumes
        CharacterCostume::NullCostume.storeCharsel(bs);
    }
    else
        m_costumes[m_current_costume_set].storeCharsel(bs);
    bs.StoreString(entity_map_name);
    bs.StorePackedBits(1,1);
}

const CharacterCostume * Character::getCurrentCostume() const
{
    assert(!m_costumes.empty());
    if(m_current_costume_set)
        return &m_costumes[m_current_costume_idx];

    return &m_costumes.front();
}

void Character::serialize_costumes(BitStream &bs, const ColorAndPartPacker *packer , bool all_costumes) const
{
    // full costume
    if(all_costumes) // this is only sent to the current player
    {
        bs.StoreBits(1,m_current_costume_set);
        if(m_current_costume_set)
        {
            bs.StoreBits(32,m_current_costume_idx);
            bs.StoreBits(32,uint32_t(m_costumes.size()));
        }
        bs.StoreBits(1,m_multiple_costumes);
        if(m_multiple_costumes)
        {
            for(const Costume & c : m_costumes)
            {
                ::serializeto(c,bs,packer);
            }
        }
        else
        {
            ::serializeto(m_costumes[m_current_costume_idx],bs,packer);
        }
        bs.StoreBits(1,m_char_data.m_supergroup_costume);
        if(m_char_data.m_supergroup_costume)
        {
            ::serializeto(*m_sg_costume,bs,packer);

            bs.StoreBits(1,m_char_data.m_using_sg_costume);
        }
    }
    else // other player's costumes we're sending only their current.
    {
        ::serializeto(*getCurrentCostume(),bs,packer);
    }
}

void Character::dumpSidekickInfo()
{
    QString msg = QString("Sidekick Info\n  has_sidekick: %1 \n  db_id: %2 \n  type: %3 ")
            .arg(m_char_data.m_sidekick.m_has_sidekick)
            .arg(m_char_data.m_sidekick.m_db_id)
            .arg(m_char_data.m_sidekick.m_type);

    qDebug().noquote() << msg;
}

void Character::dumpBuildInfo()
{
    Character &c = *this;
    QString msg = "//--------------Char Debug--------------\n  "
            + getName()
            + "\n  " + getOrigin(c)
            + "\n  " + getClass(c)
            + "\n  db_id: " + QString::number(m_db_id)
            + "\n  acct: " + QString::number(getAccountId())
            + "\n  lvl/clvl: " + QString::number(getLevel(c)) + "/" + QString::number(getCombatLevel(c))
            + "\n  inf: " + QString::number(getInf(c))
            + "\n  xp/debt: " + QString::number(getXP(c)) + "/" + QString::number(getDebt(c))
            + "\n  lfg: " + QString::number(m_char_data.m_lfg)
            + "\n  afk: " + QString::number(m_char_data.m_afk)
            + "\n  description: " + getDescription(c)
            + "\n  battleCry: " + getBattleCry(c)
            + "\n  Last Online: " + m_char_data.m_last_online;

    qDebug().noquote() << msg;
}

void Character::dump()
{
    dumpBuildInfo();
    qDebug() << "//--------------Owned Powers--------------";
    dumpOwnedPowers(m_char_data);
    qDebug() << "//-----------Owned Inspirations-----------";
    dumpInspirations(m_char_data);
    qDebug() << "//-----------Owned Enhancements-----------";
    dumpEnhancements(m_char_data);
    qDebug() << "//--------------Sidekick Info--------------";
    dumpSidekickInfo();
    qDebug() << "//------------------Tray------------------";
    m_char_data.m_trays.dump();
    qDebug() << "//-----------------Costume-----------------";
    if(!m_costumes.empty())
        getCurrentCostume()->dump();
}

void Character::recv_initial_costume( BitStream &src, const ColorAndPartPacker *packer )
{
    assert(m_costumes.size()==0);
    m_costumes.emplace_back();
    m_current_costume_idx=0;
    ::serializefrom(m_costumes.back(),src,packer);
}

void serializeStats(const Parse_CharAttrib &src,BitStream &bs, bool /*sendAbsolute*/)
{
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,0);
    bs.StorePackedBits(5,src.m_HitPoints/5.0f);
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,1);
    bs.StorePackedBits(5,src.m_Endurance/5.0f);
    bs.StoreBits(1,0); // no more data
}

void serializeFullStats(const Parse_CharAttrib &src,BitStream &bs, bool /*sendAbsolute*/)
{
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,0);
    bs.StorePackedBits(7,src.m_HitPoints);
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,1);
    bs.StorePackedBits(7,src.m_Endurance);
    bs.StoreBits(1,0); // no more data
}

void serializeLevelsStats(const Character &src,BitStream &bs, bool /*sendAbsolute*/)
{
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,0);
    bs.StorePackedBits(4,getLevel(src));
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,1);
    bs.StorePackedBits(4,getCombatLevel(src));
    bs.StoreBits(1,0); // no more data
}

void serializeStats(const Character &src,BitStream &bs, bool sendAbsolute)
{
    // Send CurrentAttribs
    // Send MaxAttribs
    // Send levels
    uint32_t field_idx=0;
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,field_idx++); // CurrentAttribs
    serializeStats(src.m_char_data.m_current_attribs,bs,sendAbsolute);
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,field_idx++); // MaxAttribs
    serializeStats(src.m_max_attribs,bs,sendAbsolute);
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,field_idx++); // levels
    serializeLevelsStats(src,bs,sendAbsolute);
    bs.StoreBits(1,0); // we have no more data
}

void serializeFullStats(const Character &src,BitStream &bs, bool sendAbsolute)
{
    uint32_t field_idx=0;
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,field_idx++); // first field is CurrentAttribs
    serializeFullStats(src.m_char_data.m_current_attribs,bs,sendAbsolute);
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,field_idx++); // first field is MaxAttribs
    serializeFullStats(src.m_max_attribs,bs,sendAbsolute);
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,field_idx++); // levels
    serializeLevelsStats(src,bs,sendAbsolute);
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,field_idx++); // ExperiencePoints
    bs.StorePackedBits(16,getXP(src));
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,field_idx++); // ExperienceDebt
    bs.StorePackedBits(16,getDebt(src));
    bs.StoreBits(1,1); // we have more data
    bs.StorePackedBits(1,field_idx++); // Influence
    bs.StorePackedBits(16,getInf(src));

    bs.StoreBits(1,0); // we have no more data
}
void Character::sendFullStats(BitStream &bs) const
{
    // if sendAbsolutoOverride
    serializeFullStats(*this,bs,false);
}

void Character::sendDescription(BitStream &bs) const
{

    qCDebug(logDescription) << "Sending Description & BattleCry"
             << "\nDescription: " << m_char_data.m_character_description
             << "\nBattle Cry: " << m_char_data.m_battle_cry;

    bs.StoreString(m_char_data.m_character_description);
    bs.StoreString(m_char_data.m_battle_cry);
}
void Character::finalizeCombatLevel()
{
    GameDataStore &data(getGameData());

    int entclass = getEntityClassIndex(data, true, getClass(*this));
    m_max_attribs.m_HitPoints = data.m_player_classes[entclass].m_AttribMaxTable[0].m_HitPoints[m_char_data.m_combat_level];
    m_max_attribs.m_Endurance = data.m_player_classes[entclass].m_AttribMaxTable[0].m_Endurance[m_char_data.m_combat_level];

}
void Character::sendTitles(BitStream &bs, NameFlag hasname, ConditionalFlag conditional) const
{
    if(hasname == NameFlag::HasName)
        bs.StoreString(getName());

    bs.StoreBits(1, m_char_data.m_has_the_prefix); // an index to a title prefix ( 0 - None; 1 - The )

    if(conditional)
    {
        storeStringConditional(bs, m_char_data.m_titles[0]); // Title 1 - generic title (first)
        storeStringConditional(bs, m_char_data.m_titles[1]); // Title 2 - origin title (second)
        storeStringConditional(bs, m_char_data.m_titles[2]); // Title 3 - yellow title (special)
    }
    else
    {
        bs.StoreString(m_char_data.m_titles[0]);             // Title 1 - generic title (first)
        bs.StoreString(m_char_data.m_titles[1]);             // Title 2 - origin title (second)
        bs.StoreString(m_char_data.m_titles[2]);             // Title 3 - yellow title (special)
    }
}

void Character::sendFriendList(BitStream &bs) const
{
    const FriendsList *fl(&m_char_data.m_friendlist);
    bs.StorePackedBits(1,1); // v2 = force_update
    bs.StorePackedBits(1,fl->m_friends_count);

    for(int i=0; i<fl->m_friends_count; ++i)
    {
        bs.StoreBits(1,fl->m_has_friends); // if false, client will skip this iteration
        bs.StorePackedBits(1,fl->m_friends[i].m_db_id);
        bs.StoreBits(1,fl->m_friends[i].m_online_status);
        bs.StoreString(fl->m_friends[i].m_name);
        bs.StorePackedBits(1,fl->m_friends[i].m_class_idx);
        bs.StorePackedBits(1,fl->m_friends[i].m_origin_idx);

        if(!fl->m_friends[i].m_online_status)
            continue; // if friend is offline, the rest is skipped

        bs.StorePackedBits(1,fl->m_friends[i].m_map_idx);
        bs.StoreString(fl->m_friends[i].m_mapname);
    }
}

void toActualCostume(const GameAccountResponseCostumeData &src, Costume &tgt)
{
    tgt.skin_color = src.skin_color;
    try
    {
        tgt.serializeFromDb(src.m_serialized_data);
    }
    catch(cereal::RapidJSONException &e)
    {
        qWarning() << e.what();
    }
    catch(std::exception &e)
    {
        qCritical() << e.what();
    }

    tgt.m_send_full_costume = false;
}

void fromActualCostume(const Costume &src,GameAccountResponseCostumeData &tgt)
{
    tgt.skin_color = src.skin_color;
    try
    {
        src.serializeToDb(tgt.m_serialized_data);
    }
    catch(cereal::RapidJSONException &e)
    {
        qWarning() << e.what();
    }
    catch(std::exception &e)
    {
        qCritical() << e.what();
    }
}

bool toActualCharacter(const GameAccountResponseCharacterData &src,
                       Character &tgt,PlayerData &player, EntityData &entity)
{
    CharacterData &  cd(tgt.m_char_data);

    tgt.m_db_id      = src.m_db_id;
    tgt.m_account_id = src.m_account_id;
    tgt.setName(src.m_name);

    try
    {
        serializeFromQString(cd,src.m_serialized_chardata);
        serializeFromQString(player,src.m_serialized_player_data);
        serializeFromQString(entity, src.m_serialized_entity_data);
    }
    catch(cereal::RapidJSONException &e)
    {
        qWarning() << e.what();
    }
    catch(std::exception &e)
    {
        qCritical() << e.what();
    }

    for (const GameAccountResponseCostumeData &costume : src.m_costumes)
    {
        tgt.m_costumes.emplace_back();
        CharacterCostume &main_costume(tgt.m_costumes.back());
        toActualCostume(costume, main_costume);
        // appearance related.
        main_costume.m_body_type = src.m_costumes.back().m_body_type;
        main_costume.m_height = src.m_costumes.back().m_height;
                main_costume.m_physique = src.m_costumes.back().m_physique;
        main_costume.setSlotIndex(costume.m_slot_index);
        main_costume.setCharacterId(costume.m_character_id);
    }
    return true;
}

bool fromActualCharacter(const Character &src,const PlayerData &player,
                         const EntityData &entity, GameAccountResponseCharacterData &tgt)
{
    const CharacterData &  cd(src.m_char_data);

    tgt.m_db_id      = src.m_db_id;
    tgt.m_account_id = src.m_account_id;
    tgt.m_name = src.getName();

    try
    {
        serializeToQString(cd, tgt.m_serialized_chardata);
        serializeToQString(player, tgt.m_serialized_player_data);
        serializeToQString(entity, tgt.m_serialized_entity_data);
    }
    catch(cereal::RapidJSONException &e)
    {
        qWarning() << e.what();
    }
    catch(std::exception &e)
    {
        qCritical() << e.what();
    }

    for (const CharacterCostume &costume : src.m_costumes)
    {
        tgt.m_costumes.emplace_back();
        GameAccountResponseCostumeData &main_costume(tgt.m_costumes.back());
        fromActualCostume(costume, main_costume);
        // appearance related.
        main_costume.m_body_type = src.m_costumes.back().m_body_type;
        main_costume.m_height = costume.m_height;
        main_costume.m_physique = costume.m_physique;
        main_costume.m_slot_index = costume.getSlotIndex();
        main_costume.m_character_id= costume.getCharacterId();
    }
    return true;
}

//! @}
