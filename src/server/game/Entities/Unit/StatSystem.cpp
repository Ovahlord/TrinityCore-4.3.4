/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Unit.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Item.h"
#include "Pet.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "World.h"
#include <G3D/g3dmath.h>
#include <numeric>

inline bool _ModifyUInt32(bool apply, uint32& baseValue, int32& amount)
{
    // If amount is negative, change sign and value of apply.
    if (amount < 0)
    {
        apply = !apply;
        amount = -amount;
    }
    if (apply)
        baseValue += amount;
    else
    {
        // Make sure we do not get uint32 overflow.
        if (amount > int32(baseValue))
            amount = baseValue;
        baseValue -= amount;
    }
    return apply;
}

/*#######################################
########                         ########
########    UNIT STAT SYSTEM     ########
########                         ########
#######################################*/

void Unit::UpdateAllResistances()
{
    for (uint8 i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateResistances(i);
}

void Unit::UpdateDamagePhysical(WeaponAttackType attType)
{
    float minDamage = 0.0f;
    float maxDamage = 0.0f;

    CalculateMinMaxDamage(attType, false, true, minDamage, maxDamage);

    switch (attType)
    {
        case BASE_ATTACK:
        default:
            SetStatFloatValue(UNIT_FIELD_MINDAMAGE, minDamage);
            SetStatFloatValue(UNIT_FIELD_MAXDAMAGE, maxDamage);
            break;
        case OFF_ATTACK:
            SetStatFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE, minDamage);
            SetStatFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE, maxDamage);
            break;
        case RANGED_ATTACK:
            SetStatFloatValue(UNIT_FIELD_MINRANGEDDAMAGE, minDamage);
            SetStatFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE, maxDamage);
            break;
    }
}

int32 Unit::GetCreatePowerValue(Powers power) const
{
    switch (power)
    {
        case POWER_MANA:
            return GetCreateMana();
        case POWER_RAGE:
            if (IsPlayer()) // only players are allowed to use rage
                return 1000;
            break;
        case POWER_FOCUS:
            return 100;
        case POWER_ENERGY:
            return 100;
        case POWER_RUNIC_POWER:
            return 1000;
        case POWER_RUNE:
            return 0;
        case POWER_SOUL_SHARDS:
            return 3;
        case POWER_ECLIPSE:
            return 100;
        case POWER_HOLY_POWER:
            return 3;
        case POWER_HEALTH:
            return 0;
        default:
            break;
    }

    return 0;
}

void Unit::UpdatePowerRegeneration(Powers powerType)
{
    uint32 powerIndex = GetPowerIndex(powerType);
    if ((powerIndex == MAX_POWERS || powerIndex >= MAX_POWERS_PER_CLASS) && powerType != POWER_RUNE)
        return;

    // Runes are not officially considered a power type for the class so we gotta bypass the rules this way
    if (powerType == POWER_RUNE && (!IsPlayer() || getClass() != CLASS_DEATH_KNIGHT))
        return;

    float powerRegenMod = GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_POWER_REGEN, powerType) / 5.f;
    float powerRegenModPct = GetTotalAuraMultiplierByMiscValue(SPELL_AURA_MOD_POWER_REGEN_PERCENT, powerType);

    switch (powerType)
    {
        case POWER_MANA:
        {
            // Get base of Mana Pool in sBaseMPGameTable
            uint32 basemana = 0, basehp = 0;
            if (IsPlayer())
                sObjectMgr->GetPlayerClassLevelInfo(getClass(), getLevel(), basemana, basehp);
            else
                basemana = GetCreateMana(); // this should also get replaced by the base mana game table in the future.

            // BaseRegen = 5% of Base Mana per five seconds
            float baseRegen = basemana / 100.f;
            // SPELL_AURA_MOD_POWER_REGEN flat bonus
            baseRegen += powerRegenMod;

            // SpiritRegen = Spirit * GTRegenMpPerSpt * Sqrt(INT) * 5
            float spiritRegen = GetStat(STAT_SPIRIT) * DBCManager::GetGtOCTRegenMPPerSpirit(getClass(), getLevel());
            if (GetStat(STAT_INTELLECT) > 0.0f)
                spiritRegen *= std::sqrt(GetStat(STAT_INTELLECT));

            // SPELL_AURA_MOD_POWER_REGEN_PERCENT pct bonus
            baseRegen *= powerRegenModPct;
            spiritRegen *= powerRegenModPct;

            // SPELL_AURA_MOD_MANA_REGEN_INTERRUPT allow some of the spirit regeneration to bypass the combat restriction
            int32 modManaRegenInterrupt = GetTotalAuraModifier(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT);

            SetFloatValue(UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER + powerIndex, baseRegen + CalculatePct(spiritRegen, modManaRegenInterrupt));
            SetFloatValue(UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER + powerIndex, baseRegen + spiritRegen);
            break;
        }
        case POWER_RUNE:
        {
            float baseRegen = DBCManager::GetBasePowerRegen(powerType, false, 0);

            // Hase Regen
            if (DBCManager::IsPowerTypeAffectedByHaste(powerType) && IsPlayer())
            {
                float hasteRegen = GetFloatValue(PLAYER_FIELD_MOD_HASTE_REGEN);
                if (G3D::fuzzyNe(hasteRegen, 0.0f))
                    baseRegen /= hasteRegen;
            }

            baseRegen += powerRegenMod;

            if (IsPlayer())
                for (uint8 i = 0; i < NUM_RUNE_TYPES; ++i)
                    SetFloatValue(PLAYER_RUNE_REGEN_1 + i, baseRegen);
            break;
        }
        default:
        {
            // Base Regen
            float peaceRegen = DBCManager::GetBasePowerRegen(powerType, false, 0);
            float combatRegen = DBCManager::GetBasePowerRegen(powerType, true, 0);

            // Hase Regen
            if (DBCManager::IsPowerTypeAffectedByHaste(powerType) && IsPlayer())
            {
                float hasteRegen = GetFloatValue(PLAYER_FIELD_MOD_HASTE_REGEN);
                if (G3D::fuzzyNe(hasteRegen, 0.0f))
                {
                    peaceRegen /= hasteRegen;
                    combatRegen /= hasteRegen;
                }
            }

            peaceRegen *= powerRegenModPct;
            combatRegen *= powerRegenModPct;

            // Subtract the base value to get the proper offset
            peaceRegen -= DBCManager::GetBasePowerRegen(powerType, false, 0);
            combatRegen -= DBCManager::GetBasePowerRegen(powerType, true, 0);

            peaceRegen += powerRegenMod;
            combatRegen += powerRegenMod;

            SetFloatValue(UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER + powerIndex, combatRegen);
            SetFloatValue(UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER + powerIndex, peaceRegen);
        }
    }
}


/*#######################################
########                         ########
########   PLAYERS STAT SYSTEM   ########
########                         ########
#######################################*/

bool Player::UpdateStats(Stats stat)
{
    if (stat > STAT_SPIRIT)
        return false;

    // value = ((base_value * base_pct) + total_value) * total_pct
    float value  = GetTotalStatValue(stat);

    SetStat(stat, int32(value));

    switch (stat)
    {
        case STAT_AGILITY:
            UpdateArmor();
            UpdateAllCritPercentages();
            UpdateDodgePercentage();
            break;
        case STAT_STAMINA:
            UpdateMaxHealth();
            break;
        case STAT_INTELLECT:
            UpdateMaxPower(POWER_MANA);
            UpdateAllSpellCritChances();
            UpdateArmor();                                  //SPELL_AURA_MOD_RESISTANCE_OF_INTELLECT_PERCENT, only armor currently
            break;
        case STAT_SPIRIT:
            break;
        default:
            break;
    }

    if (stat == STAT_STRENGTH)
        UpdateAttackPowerAndDamage(false);
    else if (stat == STAT_AGILITY)
    {
        UpdateAttackPowerAndDamage(false);
        UpdateAttackPowerAndDamage(true);
    }

    UpdateSpellDamageAndHealingBonus();
    UpdatePowerRegeneration(POWER_MANA);

    // Update ratings in exist SPELL_AURA_MOD_RATING_FROM_STAT and only depends from stat
    uint32 mask = 0;
    AuraEffectList const& modRatingFromStat = GetAuraEffectsByType(SPELL_AURA_MOD_RATING_FROM_STAT);
    for (AuraEffectList::const_iterator i = modRatingFromStat.begin(); i != modRatingFromStat.end(); ++i)
        if (Stats((*i)->GetMiscValueB()) == stat)
            mask |= (*i)->GetMiscValue();
    if (mask)
    {
        for (uint32 rating = 0; rating < MAX_COMBAT_RATING; ++rating)
            if (mask & (1 << rating))
                ApplyRatingMod(CombatRating(rating), 0, true);
    }

    return true;
}

void Player::ApplySpellPowerBonus(int32 amount, bool apply)
{
    if (HasAuraType(SPELL_AURA_OVERRIDE_SPELL_POWER_BY_AP_PCT))
        return;

    apply = _ModifyUInt32(apply, m_baseSpellPower, amount);

    // For speed just update for client
    ApplyModUInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS, amount, apply);
    for (uint8 i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
        ApplyModUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + i, amount, apply);
}

void Player::UpdateSpellDamageAndHealingBonus()
{
    // Magic damage modifiers implemented in Unit::SpellDamageBonusDone
    // This information for client side use only
    // Get healing bonus for all schools
    SetStatInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS, SpellBaseHealingBonusDone(SPELL_SCHOOL_MASK_ALL));
    // Get damage bonus for all schools
    Unit::AuraEffectList const& modDamageAuras = GetAuraEffectsByType(SPELL_AURA_MOD_DAMAGE_DONE);
    for (uint16 i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
    {
        SetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + i, std::accumulate(modDamageAuras.begin(), modDamageAuras.end(), 0, [i](int32 negativeMod, AuraEffect const* aurEff)
        {
            if (aurEff->GetAmount() < 0 && aurEff->GetMiscValue() & (1 << i))
                negativeMod += aurEff->GetAmount();
            return negativeMod;
        }));
        SetStatInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + i, SpellBaseDamageBonusDone(SpellSchoolMask(1 << i)) - GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + i));
    }
}

void Player::UpdateSpellHealingPercentDone()
{
    SetFloatValue(PLAYER_FIELD_MOD_HEALING_DONE_PCT, GetTotalAuraMultiplier(SPELL_AURA_MOD_HEALING_DONE_PERCENT));
}

void Player::UpdateSpellHealingPercentTaken()
{
    float TakenTotalMod = 1.f;
    float minval = float(GetMaxNegativeAuraModifier(SPELL_AURA_MOD_HEALING_PCT));
    if (minval)
        AddPct(TakenTotalMod, minval);

    float maxval = float(GetMaxPositiveAuraModifier(SPELL_AURA_MOD_HEALING_PCT));
    if (maxval)
        AddPct(TakenTotalMod, maxval);

    SetFloatValue(PLAYER_FIELD_MOD_HEALING_PCT, TakenTotalMod);
}

bool Player::UpdateAllStats()
{
    for (uint8 i = STAT_STRENGTH; i < MAX_STATS; ++i)
    {
        float value = GetTotalStatValue(Stats(i));
        SetStat(Stats(i), int32(value));
    }

    UpdateArmor();
    // calls UpdateAttackPowerAndDamage() in UpdateArmor for SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR
    UpdateAttackPowerAndDamage(true);
    UpdateMaxHealth();

    for (uint8 i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    UpdateAllRatings();
    UpdateAllCritPercentages();
    UpdateAllSpellCritChances();
    UpdateBlockPercentage();
    UpdateParryPercentage();
    UpdateDodgePercentage();
    UpdateSpellDamageAndHealingBonus();
    UpdatePowerRegeneration(POWER_MANA);
    UpdateExpertise(BASE_ATTACK);
    UpdateExpertise(OFF_ATTACK);
    UpdateAllResistances();

    return true;
}

void Player::ApplySpellPenetrationBonus(int32 amount, bool apply)
{
    ApplyModInt32Value(PLAYER_FIELD_MOD_TARGET_RESISTANCE, -amount, apply);
    m_spellPenetrationItemMod += apply ? amount : -amount;
}

void Player::UpdateResistances(uint32 school)
{
    if (school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();
}

void Player::UpdateArmor()
{
    UnitMods unitMod = UNIT_MOD_ARMOR;

    float value = GetFlatModifierValue(unitMod, BASE_VALUE);    // base armor (from items)
    value *= GetPctModifierValue(unitMod, BASE_PCT);            // armor percent from items
    // value += GetStat(STAT_AGILITY) * 2.0f;                      // armor bonus from stats (deprecated since 4.x)
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE);

    //add dynamic flat mods
    AuraEffectList const& mResbyIntellect = GetAuraEffectsByType(SPELL_AURA_MOD_RESISTANCE_OF_STAT_PERCENT);
    for (AuraEffectList::const_iterator i = mResbyIntellect.begin(); i != mResbyIntellect.end(); ++i)
    {
        if ((*i)->GetMiscValue() & SPELL_SCHOOL_MASK_NORMAL)
            value += CalculatePct(GetStat(Stats((*i)->GetMiscValueB())), (*i)->GetAmount());
    }

    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetArmor(int32(value));
    UpdateAttackPowerAndDamage();                           // armor dependent auras update for SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR
}

float Player::GetHealthBonusFromStamina()
{
    // Taken from PaperDollFrame.lua - 4.3.4.15595
    float ratio = 10.0f;
    if (gtOCTHpPerStaminaEntry const* hpBase = sGtOCTHpPerStaminaStore.LookupEntry((getClass() - 1) * GT_MAX_LEVEL + getLevel() - 1))
        ratio = hpBase->ratio;

    float stamina = GetStat(STAT_STAMINA);
    float baseStam = std::min(20.0f, stamina);
    float moreStam = stamina - baseStam;

    return baseStam + moreStam * ratio;
}

float Player::GetManaBonusFromIntellect()
{
    // Taken from PaperDollFrame.lua - 4.3.4.15595
    float intellect = GetStat(STAT_INTELLECT);

    float baseInt = std::min(20.0f, intellect);
    float moreInt = intellect - baseInt;

    return baseInt + (moreInt * 15.0f);
}

void Player::UpdateMaxHealth()
{
    UnitMods unitMod = UNIT_MOD_HEALTH;

    float value = GetFlatModifierValue(unitMod, BASE_VALUE) + GetCreateHealth();
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE) + GetHealthBonusFromStamina();
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetMaxHealth((uint32)value);
}

uint32 Player::GetPowerIndex(Powers power) const
{
    return sDBCManager.GetPowerIndexByClass(power, getClass());
}

void Player::UpdateMaxPower(Powers power)
{
    uint32 powerIndex = GetPowerIndex(power);
    if (powerIndex == MAX_POWERS || powerIndex >= MAX_POWERS_PER_CLASS)
        return;

    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + AsUnderlyingType(power));

    float bonusPower = (power == POWER_MANA && GetCreatePowerValue(power) > 0) ? GetManaBonusFromIntellect() : 0;

    float value = GetFlatModifierValue(unitMod, BASE_VALUE) + GetCreatePowerValue(power);
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE) +  bonusPower;
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, (int32)std::lroundf(value));
}

void Player::UpdateAttackPowerAndDamage(bool ranged)
{
    float val2 = 0.0f;
    float level = float(getLevel());

    ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(getClass());
    UnitMods unitMod = ranged ? UNIT_MOD_ATTACK_POWER_RANGED : UNIT_MOD_ATTACK_POWER;

    uint16 index = ranged ? UNIT_FIELD_RANGED_ATTACK_POWER : UNIT_FIELD_ATTACK_POWER;
    uint16 index_mod_pos = ranged ? UNIT_FIELD_RANGED_ATTACK_POWER_MOD_POS : UNIT_FIELD_ATTACK_POWER_MOD_POS;
    uint16 index_mod_neg = ranged ? UNIT_FIELD_RANGED_ATTACK_POWER_MOD_NEG : UNIT_FIELD_ATTACK_POWER_MOD_NEG;
    uint16 index_mult = ranged ? UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER : UNIT_FIELD_ATTACK_POWER_MULTIPLIER;

    if (ranged)
        val2 = (level + std::max(GetStat(STAT_AGILITY) - 10.0f, 0.0f)) * entry->RangedAttackPowerPerAgility;
    else
    {
        float strengthValue = std::max((GetStat(STAT_STRENGTH) - 10.0f) * entry->AttackPowerPerStrength, 0.0f);
        float agilityValue = std::max((GetStat(STAT_AGILITY) - 10.0f) * entry->AttackPowerPerAgility, 0.0f);

        // Druids in Bear and Cat form get two points attack power per agility point
        if (GetShapeshiftForm() == FORM_BEAR || GetShapeshiftForm() == FORM_CAT)
            agilityValue = std::max((GetStat(STAT_AGILITY) - 10.0f) * 2, 0.0f);

        val2 = strengthValue + agilityValue;
    }

    SetStatFlatModifier(unitMod, BASE_VALUE, val2);

    float base_attPower  = GetFlatModifierValue(unitMod, BASE_VALUE) * GetPctModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetFlatModifierValue(unitMod, TOTAL_VALUE);

    //add dynamic flat mods
    if (!ranged)
    {
        AuraEffectList const& mAPbyArmor = GetAuraEffectsByType(SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR);
        for (AuraEffectList::const_iterator iter = mAPbyArmor.begin(); iter != mAPbyArmor.end(); ++iter)
            // always: ((*i)->GetModifier()->m_miscvalue == 1 == SPELL_SCHOOL_MASK_NORMAL)
            attPowerMod += int32(GetArmor() / (*iter)->GetAmount());
    }

    float attPowerMultiplier = GetPctModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    SetInt32Value(index, (uint32)base_attPower);                                  //UNIT_FIELD_(RANGED)_ATTACK_POWER field
    SetInt32Value(index_mod_pos, uint32(attPowerMod > 0.f ? attPowerMod : 0.f));  //UNIT_FIELD_(RANGED)_ATTACK_POWER_MOD_POS field
    SetInt32Value(index_mod_neg, uint32(attPowerMod < 0.f ? -attPowerMod : 0.f)); //UNIT_FIELD_(RANGED)_ATTACK_POWER_MOD_NEG field
    SetFloatValue(index_mult, attPowerMultiplier);                                //UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER field

    //automatically update weapon damage after attack power modification
    if (ranged)
        UpdateDamagePhysical(RANGED_ATTACK);
    else
    {
        UpdateDamagePhysical(BASE_ATTACK);
        if (CanDualWield() && haveOffhandWeapon())           //allow update offhand damage only if player knows DualWield Spec and has equipped offhand weapon
            UpdateDamagePhysical(OFF_ATTACK);

        if (HasAuraType(SPELL_AURA_OVERRIDE_SPELL_POWER_BY_AP_PCT))
            UpdateSpellDamageAndHealingBonus();
    }
}

void Player::CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage) const
{
    UnitMods unitMod;

    switch (attType)
    {
        case BASE_ATTACK:
        default:
            unitMod = UNIT_MOD_DAMAGE_MAINHAND;
            break;
        case OFF_ATTACK:
            unitMod = UNIT_MOD_DAMAGE_OFFHAND;
            break;
        case RANGED_ATTACK:
            unitMod = UNIT_MOD_DAMAGE_RANGED;
            break;
    }

    float const attackPowerMod = std::max(GetAPMultiplier(attType, normalized), 0.25f);

    float baseValue  = GetFlatModifierValue(unitMod, BASE_VALUE) + GetTotalAttackPowerValue(attType) / 14.0f * attackPowerMod;
    float basePct    = GetPctModifierValue(unitMod, BASE_PCT);
    float totalValue = GetFlatModifierValue(unitMod, TOTAL_VALUE);
    float totalPct   = addTotalPct ? GetPctModifierValue(unitMod, TOTAL_PCT) : 1.0f;

    float weaponMinDamage = GetWeaponDamageRange(attType, MINDAMAGE);
    float weaponMaxDamage = GetWeaponDamageRange(attType, MAXDAMAGE);

    if (IsInFeralForm()) // check if player is druid and in cat or bear forms
    {
        float weaponSpeed = BASE_ATTACK_TIME / 1000.f;
        if (Item* weapon = GetWeaponForAttack(BASE_ATTACK, true))
            weaponSpeed =  weapon->GetTemplate()->GetDelay() / 1000;

        if (GetShapeshiftForm() == FORM_CAT)
        {
            weaponMinDamage = weaponMinDamage / weaponSpeed;
            weaponMaxDamage = weaponMaxDamage / weaponSpeed;
        }
        else if (GetShapeshiftForm() == FORM_BEAR)
        {
            weaponMinDamage = weaponMinDamage / weaponSpeed + weaponMinDamage / 2.5;
            weaponMaxDamage = weaponMinDamage / weaponSpeed + weaponMaxDamage / 2.5;
        }
    }
    else if (!CanUseAttackType(attType)) // check if player not in form but still can't use (disarm case)
    {
        // cannot use ranged/off attack, set values to 0
        if (attType != BASE_ATTACK)
        {
            minDamage = 0;
            maxDamage = 0;
            return;
        }
        weaponMinDamage = BASE_MINDAMAGE;
        weaponMaxDamage = BASE_MAXDAMAGE;
    }
    /*
    TODO: Is this still needed after ammo has been removed?
    else if (attType == RANGED_ATTACK) // add ammo DPS to ranged damage
    {
        weaponMinDamage += GetAmmoDPS() * attackPowerMod;
        weaponMaxDamage += GetAmmoDPS() * attackPowerMod;
    }*/

    minDamage = ((weaponMinDamage + baseValue) * basePct + totalValue) * totalPct;
    maxDamage = ((weaponMaxDamage + baseValue) * basePct + totalValue) * totalPct;
}

void Player::UpdateBlockPercentage()
{
    // No block
    float value = 0.0f;
    if (CanBlock())
    {
        // Base value
        value = 5.0f;
        // Increase from SPELL_AURA_MOD_BLOCK_PERCENT aura
        value += GetTotalAuraModifier(SPELL_AURA_MOD_BLOCK_PERCENT);
        // Increase from rating
        value += GetRatingBonusValue(CR_BLOCK);

        if (sWorld->getBoolConfig(CONFIG_STATS_LIMITS_ENABLE))
             value = value > sWorld->getFloatConfig(CONFIG_STATS_LIMITS_BLOCK) ? sWorld->getFloatConfig(CONFIG_STATS_LIMITS_BLOCK) : value;

        value = value < 0.0f ? 0.0f : value;
    }
    SetStatFloatValue(PLAYER_BLOCK_PERCENTAGE, value);
}

void Player::UpdateCritPercentage(WeaponAttackType attType)
{
    BaseModGroup modGroup;
    uint16 index;
    CombatRating cr;

    switch (attType)
    {
        case OFF_ATTACK:
            modGroup = OFFHAND_CRIT_PERCENTAGE;
            index = PLAYER_OFFHAND_CRIT_PERCENTAGE;
            cr = CR_CRIT_MELEE;
            break;
        case RANGED_ATTACK:
            modGroup = RANGED_CRIT_PERCENTAGE;
            index = PLAYER_RANGED_CRIT_PERCENTAGE;
            cr = CR_CRIT_RANGED;
            break;
        case BASE_ATTACK:
        default:
            modGroup = CRIT_PERCENTAGE;
            index = PLAYER_CRIT_PERCENTAGE;
            cr = CR_CRIT_MELEE;
            break;
    }

    // flat = bonus from crit auras, pct = bonus from agility, combat rating = mods from items
    float value = GetBaseModValue(modGroup, FLAT_MOD) + GetBaseModValue(modGroup, PCT_MOD) + GetRatingBonusValue(cr);

    // Modify crit from weapon skill and maximized defense skill of same level victim difference
    value += (int32(GetMaxSkillValueForLevel()) - int32(GetMaxSkillValueForLevel())) * 0.04f;

    if (sWorld->getBoolConfig(CONFIG_STATS_LIMITS_ENABLE))
         value = value > sWorld->getFloatConfig(CONFIG_STATS_LIMITS_CRIT) ? sWorld->getFloatConfig(CONFIG_STATS_LIMITS_CRIT) : value;

    value = std::max(0.0f, value);
    SetStatFloatValue(index, value);
}

void Player::UpdateAllCritPercentages()
{
    float value = GetMeleeCritFromAgility();

    SetBaseModPctValue(CRIT_PERCENTAGE, value);
    SetBaseModPctValue(OFFHAND_CRIT_PERCENTAGE, value);
    SetBaseModPctValue(RANGED_CRIT_PERCENTAGE, value);

    UpdateCritPercentage(BASE_ATTACK);
    UpdateCritPercentage(OFF_ATTACK);
    UpdateCritPercentage(RANGED_ATTACK);
}

void Player::UpdateMastery()
{
    if (!CanUseMastery())
    {
        SetFloatValue(PLAYER_MASTERY, 0.0f);
        return;
    }

    float value = GetTotalAuraModifier(SPELL_AURA_MASTERY);
    value += GetRatingBonusValue(CR_MASTERY);
    SetFloatValue(PLAYER_MASTERY, value);

    TalentTabEntry const* talentTab = sTalentTabStore.LookupEntry(GetPrimaryTalentTree(GetActiveSpec()));
    if (!talentTab)
        return;

    for (uint32 i = 0; i < MAX_MASTERY_SPELLS; ++i)
    {
        if (!talentTab->MasterySpellID[i])
            continue;

        if (Aura* aura = GetAura(talentTab->MasterySpellID[i]))
        {
            for (uint32 j = 0; j < MAX_SPELL_EFFECTS; ++j)
            {
                if (!aura->HasEffect(j))
                    continue;

                float mult = aura->GetSpellInfo()->Effects[j].BonusMultiplier;
                if (G3D::fuzzyEq(mult, 0.0f))
                    continue;

                aura->GetEffect(j)->ChangeAmount(int32(value * aura->GetSpellInfo()->Effects[j].BonusMultiplier));
            }
        }
    }
}

float const m_diminishing_k[MAX_CLASSES] =
{
    0.9560f,  // Warrior
    0.9560f,  // Paladin
    0.9880f,  // Hunter
    0.9880f,  // Rogue
    0.9830f,  // Priest
    0.9560f,  // DK
    0.9880f,  // Shaman
    0.9830f,  // Mage
    0.9830f,  // Warlock
    0.0f,     // ??
    0.9720f   // Druid
};

float CalculateDiminishingReturns(float const (&capArray)[MAX_CLASSES], uint8 playerClass, float nonDiminishValue, float diminishValue)
{
    //  1     1     k              cx
    // --- = --- + --- <=> x' = --------
    //  x'    c     x            x + ck

    // where:
    // k  is m_diminishing_k for that class
    // c  is capArray for that class
    // x  is chance before DR (diminishValue)
    // x' is chance after DR (our result)

    uint32 const classIdx = playerClass - 1;

    float const k = m_diminishing_k[classIdx];
    float const c = capArray[classIdx];

    float result = c * diminishValue / (diminishValue + c * k);
    result += nonDiminishValue;
    return result;
}

float const parry_cap[MAX_CLASSES] =
{
    65.631440f,     // Warrior
    65.631440f,     // Paladin
    145.560408f,    // Hunter
    145.560408f,    // Rogue
    0.0f,           // Priest
    65.631440f,     // DK
    145.560408f,    // Shaman
    0.0f,           // Mage
    0.0f,           // Warlock
    0.0f,           // ??
    0.0f            // Druid
};

void Player::UpdateParryPercentage()
    {
    // No parry
    float value = 0.0f;
    uint32 pclass = getClass() - 1;
    if (CanParry() && parry_cap[pclass] > 0.0f)
    {
        float nondiminishing  = 5.0f;
        // Parry from rating
        float diminishing = GetRatingBonusValue(CR_PARRY);
        // Parry from SPELL_AURA_MOD_PARRY_PERCENT aura
        nondiminishing += GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT);
        // apply diminishing formula to diminishing parry chance
        value = CalculateDiminishingReturns(parry_cap, getClass(), nondiminishing, diminishing);

        if (sWorld->getBoolConfig(CONFIG_STATS_LIMITS_ENABLE))
             value = value > sWorld->getFloatConfig(CONFIG_STATS_LIMITS_PARRY) ? sWorld->getFloatConfig(CONFIG_STATS_LIMITS_PARRY) : value;

        value = value < 0.0f ? 0.0f : value;
    }
    SetStatFloatValue(PLAYER_PARRY_PERCENTAGE, value);
}

float const dodge_cap[MAX_CLASSES] =
{
    65.631440f,     // Warrior
    65.631440f,     // Paladin
    145.560408f,    // Hunter
    145.560408f,    // Rogue
    150.375940f,    // Priest
    65.631440f,     // DK
    145.560408f,    // Shaman
    150.375940f,    // Mage
    150.375940f,    // Warlock
    0.0f,           // ??
    116.890707f     // Druid
};

void Player::UpdateDodgePercentage()
{
    float diminishing = 0.0f, nondiminishing = 0.0f;
    GetDodgeFromAgility(diminishing, nondiminishing);
    // Dodge from SPELL_AURA_MOD_DODGE_PERCENT aura
    nondiminishing += GetTotalAuraModifier(SPELL_AURA_MOD_DODGE_PERCENT);
    // Dodge from rating
    diminishing += GetRatingBonusValue(CR_DODGE);
    // apply diminishing formula to diminishing dodge chance
    float value = CalculateDiminishingReturns(dodge_cap, getClass(), nondiminishing, diminishing);

    if (sWorld->getBoolConfig(CONFIG_STATS_LIMITS_ENABLE))
         value = value > sWorld->getFloatConfig(CONFIG_STATS_LIMITS_DODGE) ? sWorld->getFloatConfig(CONFIG_STATS_LIMITS_DODGE) : value;

    value = value < 0.0f ? 0.0f : value;
    SetStatFloatValue(PLAYER_DODGE_PERCENTAGE, value);
}

void Player::UpdateSpellCritChance(uint32 school)
{
    // For normal school set zero crit chance
    if (school == SPELL_SCHOOL_NORMAL)
    {
        SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1, 0.0f);
        return;
    }
    // For others recalculate it from:
    float crit = 0.0f;
    // Crit from Intellect
    crit += GetSpellCritFromIntellect();
    // Increase crit from SPELL_AURA_MOD_SPELL_CRIT_CHANCE
    crit += GetTotalAuraModifier(SPELL_AURA_MOD_SPELL_CRIT_CHANCE);
    // Increase crit from SPELL_AURA_MOD_CRIT_PCT
    crit += GetTotalAuraModifier(SPELL_AURA_MOD_CRIT_PCT);
    // Increase crit by school from SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL
    crit += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL, 1<<school);
    // Increase crit from spell crit ratings
    crit += GetRatingBonusValue(CR_CRIT_SPELL);

    // Store crit value
    SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + school, crit);
}

void Player::UpdateMeleeHitChances()
{
    m_modMeleeHitChance = GetRatingBonusValue(CR_HIT_MELEE);
}

void Player::UpdateRangedHitChances()
{
    m_modRangedHitChance = GetRatingBonusValue(CR_HIT_RANGED);
}

void Player::UpdateSpellHitChances()
{
    m_modSpellHitChance = (float)GetTotalAuraModifier(SPELL_AURA_MOD_SPELL_HIT_CHANCE);
    SetFloatValue(PLAYER_FIELD_UI_SPELL_HIT_MODIFIER, m_modSpellHitChance);
    m_modSpellHitChance += GetRatingBonusValue(CR_HIT_SPELL);
}

void Player::UpdateHitChances()
{
    float modifier = (float)GetTotalAuraModifier(SPELL_AURA_MOD_HIT_CHANCE);
    SetFloatValue(PLAYER_FIELD_UI_HIT_MODIFIER, modifier);
}

void Player::UpdateAllSpellCritChances()
{
    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateSpellCritChance(i);
}

void Player::UpdateExpertise(WeaponAttackType attack)
{
    if (attack == RANGED_ATTACK)
        return;

    int32 expertise = int32(GetRatingBonusValue(CR_EXPERTISE));

    Item* weapon = GetWeaponForAttack(attack, true);

    AuraEffectList const& expAuras = GetAuraEffectsByType(SPELL_AURA_MOD_EXPERTISE);
    for (AuraEffectList::const_iterator itr = expAuras.begin(); itr != expAuras.end(); ++itr)
    {
        // item neutral spell
        if ((*itr)->GetSpellInfo()->EquippedItemClass == -1)
            expertise += (*itr)->GetAmount();
        // item dependent spell
        else if (weapon && weapon->IsFitToSpellRequirements((*itr)->GetSpellInfo()))
            expertise += (*itr)->GetAmount();
    }

    if (expertise < 0)
        expertise = 0;

    switch (attack)
    {
        case BASE_ATTACK:
            SetUInt32Value(PLAYER_EXPERTISE, expertise);
            break;
        case OFF_ATTACK:
            SetUInt32Value(PLAYER_OFFHAND_EXPERTISE, expertise);
            break;
        default:
            break;
    }
}

void Player::ApplyManaRegenBonus(int32 amount, bool apply)
{
    _ModifyUInt32(apply, m_baseManaRegen, amount);
    UpdatePowerRegeneration(POWER_MANA);
}

void Player::ApplyHealthRegenBonus(int32 amount, bool apply)
{
    _ModifyUInt32(apply, m_baseHealthRegen, amount);
}

void Player::_ApplyAllStatBonuses()
{
    SetCanModifyStats(false);

    _ApplyAllAuraStatMods();
    _ApplyAllItemMods();

    SetCanModifyStats(true);

    UpdateAllStats();
}

void Player::_RemoveAllStatBonuses()
{
    SetCanModifyStats(false);

    _RemoveAllItemMods();
    _RemoveAllAuraStatMods();

    SetCanModifyStats(true);

    UpdateAllStats();
}

/*#######################################
########                         ########
########    MOBS STAT SYSTEM     ########
########                         ########
#######################################*/

bool Creature::UpdateStats(Stats /*stat*/)
{
    return true;
}

bool Creature::UpdateAllStats()
{
    UpdateMaxHealth();
    UpdateAttackPowerAndDamage();
    UpdateAttackPowerAndDamage(true);

    for (uint8 i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    UpdateAllResistances();

    return true;
}

void Creature::UpdateResistances(uint32 school)
{
    if (school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();
}

void Creature::UpdateArmor()
{
    float value = GetTotalAuraModValue(UNIT_MOD_ARMOR);
    SetArmor(int32(value));
}

void Creature::UpdateMaxHealth()
{
    float value = GetTotalAuraModValue(UNIT_MOD_HEALTH);
    SetMaxHealth(uint32(value));
}

uint32 Creature::GetPowerIndex(Powers power) const
{
    if (power == GetPowerType())
        return 0;
    if (power == POWER_ALTERNATE_POWER)
        return 1;
    return MAX_POWERS;
}

void Creature::UpdateMaxPower(Powers power)
{
    if (GetPowerIndex(power) == MAX_POWERS)
        return;

    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + AsUnderlyingType(power));

    float value = GetFlatModifierValue(unitMod, BASE_VALUE) + GetCreatePowerValue(power);
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE);
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, uint32(std::lroundf(value)));
}

void Creature::UpdateAttackPowerAndDamage(bool ranged)
{
    UnitMods unitMod = ranged ? UNIT_MOD_ATTACK_POWER_RANGED : UNIT_MOD_ATTACK_POWER;

    uint16 index = UNIT_FIELD_ATTACK_POWER;
    uint16 indexModPos = UNIT_FIELD_ATTACK_POWER_MOD_POS;
    uint16 indexModNeg = UNIT_FIELD_ATTACK_POWER_MOD_NEG;
    uint16 indexMulti = UNIT_FIELD_ATTACK_POWER_MULTIPLIER;

    if (ranged)
    {
        index = UNIT_FIELD_RANGED_ATTACK_POWER;
        indexModPos = UNIT_FIELD_RANGED_ATTACK_POWER_MOD_POS;
        indexModNeg = UNIT_FIELD_RANGED_ATTACK_POWER_MOD_NEG;
        indexMulti = UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER;
    }

    float baseAttackPower       = GetFlatModifierValue(unitMod, BASE_VALUE) * GetPctModifierValue(unitMod, BASE_PCT);
    float attackPowerMod        = GetFlatModifierValue(unitMod, TOTAL_VALUE);
    float attackPowerMultiplier = GetPctModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    SetInt32Value(index, uint32(baseAttackPower));                                    // UNIT_FIELD_(RANGED)_ATTACK_POWER
    SetInt32Value(indexModPos, uint32(attackPowerMod > 0.f ? attackPowerMod : 0.f));  // UNIT_FIELD_(RANGED)_ATTACK_POWER_MOD_POS
    SetInt32Value(indexModNeg, uint32(attackPowerMod < 0.f ? -attackPowerMod : 0.f)); // UNIT_FIELD_(RANGED)_ATTACK_POWER_MOD_NEG
    SetFloatValue(indexMulti, attackPowerMultiplier);                                 // UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER

    // automatically update weapon damage after attack power modification
    if (ranged)
        UpdateDamagePhysical(RANGED_ATTACK);
    else
    {
        UpdateDamagePhysical(BASE_ATTACK);
        UpdateDamagePhysical(OFF_ATTACK);
    }
}

void Creature::CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage) const
{
    float variance = 1.0f;
    UnitMods unitMod;
    switch (attType)
    {
        case BASE_ATTACK:
        default:
            variance = GetCreatureTemplate()->BaseVariance;
            unitMod = UNIT_MOD_DAMAGE_MAINHAND;
            break;
        case OFF_ATTACK:
            variance = GetCreatureTemplate()->BaseVariance;
            unitMod = UNIT_MOD_DAMAGE_OFFHAND;
            break;
        case RANGED_ATTACK:
            variance = GetCreatureTemplate()->RangeVariance;
            unitMod = UNIT_MOD_DAMAGE_RANGED;
            break;
    }

    if (attType == OFF_ATTACK && !haveOffhandWeapon())
    {
        minDamage = 0.0f;
        maxDamage = 0.0f;
        return;
    }

    float weaponMinDamage = GetWeaponDamageRange(attType, MINDAMAGE);
    float weaponMaxDamage = GetWeaponDamageRange(attType, MAXDAMAGE);

    if (!CanUseAttackType(attType)) // disarm case
    {
        weaponMinDamage = 0.0f;
        weaponMaxDamage = 0.0f;
    }

    float attackPower      = GetTotalAttackPowerValue(attType);
    float attackSpeedMulti = GetAPMultiplier(attType, normalized);
    float baseValue        = GetFlatModifierValue(unitMod, BASE_VALUE) + (attackPower / 14.0f) * variance;
    float basePct          = GetPctModifierValue(unitMod, BASE_PCT) * attackSpeedMulti;
    float totalValue       = GetFlatModifierValue(unitMod, TOTAL_VALUE);
    float totalPct         = addTotalPct ? GetPctModifierValue(unitMod, TOTAL_PCT) : 1.0f;
    float dmgMultiplier    = GetCreatureTemplate()->ModDamage; // = ModDamage * _GetDamageMod(rank);

    minDamage = ((weaponMinDamage + baseValue) * dmgMultiplier * basePct + totalValue) * totalPct;
    maxDamage = ((weaponMaxDamage + baseValue) * dmgMultiplier * basePct + totalValue) * totalPct;
}

/*#######################################
########                         ########
########    PETS STAT SYSTEM     ########
########                         ########
#######################################*/

bool Guardian::UpdateStats(Stats stat)
{
    if (stat >= MAX_STATS)
        return false;

    // value = ((base_value * base_pct) + total_value) * total_pct
    float value  = GetTotalStatValue(stat);
    //ApplyStatBuffMod(stat, m_statFromOwner[stat], false);
    float ownersBonus = 0.0f;

    SetStat(stat, int32(value));
    m_statFromOwner[stat] = ownersBonus;
    UpdateStatBuffMod(stat);

    switch (stat)
    {
        case STAT_STRENGTH:
            UpdateAttackPowerAndDamage();
            break;
        case STAT_AGILITY:
            UpdateArmor();
            break;
        case STAT_STAMINA:
            UpdateMaxHealth();
            break;
        case STAT_INTELLECT:
            UpdateMaxPower(POWER_MANA);
            break;
        case STAT_SPIRIT:
            break;
        default:
            break;
    }

    return true;
}

bool Guardian::UpdateAllStats()
{
    UpdateMaxHealth();

    for (uint8 i = STAT_STRENGTH; i < MAX_STATS; ++i)
        UpdateStats(Stats(i));

    for (uint8 i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    UpdateAllResistances();

    return true;
}

void Guardian::UpdateResistances(uint32 school)
{
    if (school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();
}

void Guardian::UpdateArmor()
{
    float value = 0.0f;
    UnitMods unitMod = UNIT_MOD_ARMOR;

    value  = GetFlatModifierValue(unitMod, BASE_VALUE);
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    //value += GetStat(STAT_AGILITY) * 2.0f; (deprecated since 4.x)
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE);
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);


    SetArmor(int32(value));
}

void Guardian::UpdateMaxHealth()
{
    UnitMods unitMod = UNIT_MOD_HEALTH;
    float stamina = GetStat(STAT_STAMINA) - GetCreateStat(STAT_STAMINA);
    float multiplicator = 10.0f;

    float value = GetFlatModifierValue(unitMod, BASE_VALUE) + GetCreateHealth();
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE) + stamina * multiplicator;
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetMaxHealth((uint32)value);
}

void Guardian::UpdateMaxPower(Powers power)
{
    if (GetPowerIndex(power) == MAX_POWERS)
        return;

    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + AsUnderlyingType(power));

    float addValue = (power == POWER_MANA) ? GetStat(STAT_INTELLECT) - GetCreateStat(STAT_INTELLECT) : 0.0f;
    float multiplicator = 15.0f;

    float value  = GetFlatModifierValue(unitMod, BASE_VALUE) + GetCreatePowerValue(power);
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE) + addValue * multiplicator;
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, int32(value));
}

void Guardian::UpdateAttackPowerAndDamage(bool ranged)
{
    if (ranged)
        return;

    float ap_per_strength = 2.0f;
    float val = GetStat(STAT_STRENGTH) - 20.0f;

    val *= ap_per_strength;

    UnitMods unitMod = UNIT_MOD_ATTACK_POWER;

    SetStatFlatModifier(UNIT_MOD_ATTACK_POWER, BASE_VALUE, val);

    // in BASE_VALUE of UNIT_MOD_ATTACK_POWER for creatures we store data of meleeattackpower field in DB
    float base_attPower  = GetFlatModifierValue(unitMod, BASE_VALUE) * GetPctModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetFlatModifierValue(unitMod, TOTAL_VALUE);
    float attPowerMultiplier = GetPctModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    // UNIT_FIELD_(RANGED)_ATTACK_POWER field
    SetInt32Value(UNIT_FIELD_ATTACK_POWER, int32(base_attPower));
    // UNIT_FIELD_(RANGED)_ATTACK_POWER_MOD_POS field
    SetInt32Value(UNIT_FIELD_ATTACK_POWER_MOD_POS, int32(attPowerMod > 0.f ? attPowerMod : 0.f));
    // UNIT_FIELD_(RANGED)_ATTACK_POWER_MOD_NEG field
    SetInt32Value(UNIT_FIELD_ATTACK_POWER_MOD_NEG, int32(attPowerMod < 0.f ? -attPowerMod : 0.f));
    // UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER field
    SetFloatValue(UNIT_FIELD_ATTACK_POWER_MULTIPLIER, attPowerMultiplier);

    // automatically update weapon damage after attack power modification
    UpdateDamagePhysical(BASE_ATTACK);

    // update pet spell power
    int32 spellDamage = 0;
    for (auto itr : GetAuraEffectsByType(SPELL_AURA_MOD_DAMAGE_DONE))
        spellDamage += itr->GetAmount();

    SetBonusDamage(spellDamage);
}

void Guardian::UpdateDamagePhysical(WeaponAttackType attType)
{
    if (attType > BASE_ATTACK)
        return;

    float bonusDamage = 0.0f;
    if (m_owner->GetTypeId() == TYPEID_PLAYER)
    {
        //force of nature
        if (GetEntry() == ENTRY_TREANT)
        {
            int32 spellDmg = int32(m_owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + AsUnderlyingType(SPELL_SCHOOL_NATURE))) + m_owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + AsUnderlyingType(SPELL_SCHOOL_NATURE));
            if (spellDmg > 0)
                bonusDamage = spellDmg * 0.09f;
        }
        //greater fire elemental
        else if (GetEntry() == ENTRY_FIRE_ELEMENTAL)
        {
            int32 spellDmg = int32(m_owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + AsUnderlyingType(SPELL_SCHOOL_FIRE))) + m_owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + AsUnderlyingType(SPELL_SCHOOL_FIRE));
            if (spellDmg > 0)
                bonusDamage = spellDmg * 0.4f;
        }
    }

    UnitMods unitMod = UNIT_MOD_DAMAGE_MAINHAND;

    float att_speed = float(GetAttackTime(BASE_ATTACK))/1000.0f;

    float base_value  = GetFlatModifierValue(unitMod, BASE_VALUE) + GetTotalAttackPowerValue(attType) / 14.0f * att_speed + bonusDamage;
    float base_pct    = GetPctModifierValue(unitMod, BASE_PCT);
    float total_value = GetFlatModifierValue(unitMod, TOTAL_VALUE);
    float total_pct   = GetPctModifierValue(unitMod, TOTAL_PCT);

    float weapon_mindamage = GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
    float weapon_maxdamage = GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);

    float mindamage = ((base_value + weapon_mindamage) * base_pct + total_value) * total_pct;
    float maxdamage = ((base_value + weapon_maxdamage) * base_pct + total_value) * total_pct;

    /// @todo: remove this
    Unit::AuraEffectList const& mDummy = GetAuraEffectsByType(SPELL_AURA_MOD_ATTACKSPEED);
    for (Unit::AuraEffectList::const_iterator itr = mDummy.begin(); itr != mDummy.end(); ++itr)
    {
        switch ((*itr)->GetSpellInfo()->Id)
        {
            case 61682:
            case 61683:
                AddPct(mindamage, -(*itr)->GetAmount());
                AddPct(maxdamage, -(*itr)->GetAmount());
                break;
            default:
                break;
        }
    }

    SetStatFloatValue(UNIT_FIELD_MINDAMAGE, mindamage);
    SetStatFloatValue(UNIT_FIELD_MAXDAMAGE, maxdamage);
}

void Guardian::SetBonusDamage(int32 damage)
{
    //m_bonusSpellDamage = damage;
    if (GetOwner()->GetTypeId() == TYPEID_PLAYER)
        GetOwner()->SetUInt32Value(PLAYER_PET_SPELL_POWER, damage);
}
