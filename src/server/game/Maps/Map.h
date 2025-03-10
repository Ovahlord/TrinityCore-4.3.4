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

#ifndef TRINITY_MAP_H
#define TRINITY_MAP_H

#include "Define.h"

#include "Cell.h"
#include "DynamicTree.h"
#include "GridDefines.h"
#include "GridRefManager.h"
#include "MapDefines.h"
#include "MapReference.h"
#include "MapRefManager.h"
#include "MPSCQueue.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include "SpawnData.h"
#include "Timer.h"
#include "WorldStateDefines.h"
#include "Transaction.h"
#include "Weather.h"
#include <bitset>
#include <list>
#include <memory>

class Battleground;
class BattlegroundMap;
class CreatureGroup;
class Group;
class InstanceMap;
class InstanceSave;
class InstanceScript;
class Object;
class PhaseShift;
class Player;
class SpawnedPoolData;
class TempSummon;
class TerrainInfo;
class Transport;
class Unit;
class Weather;
class WorldObject;
class WorldPacket;
struct MapDifficulty;
struct MapEntry;
struct Position;
struct ScriptAction;
struct ScriptInfo;
struct SummonPropertiesEntry;
enum Difficulty : uint8;
namespace Trinity { struct ObjectUpdater; }
namespace VMAP { enum class ModelIgnoreFlags : uint32; }

namespace WorldPackets
{
    namespace WorldState
    {
        struct WorldStateInfo;
    }
}

struct ScriptAction
{
    ObjectGuid sourceGUID;
    ObjectGuid targetGUID;
    ObjectGuid ownerGUID;                                   ///> owner of source if source is item
    ScriptInfo const* script;                               ///> pointer to static script data
};

enum LevelRequirementVsMode
{
    LEVELREQUIREMENT_HEROIC = 70
};

struct ZoneDynamicInfo
{
    ZoneDynamicInfo();

    uint32 MusicId;
    std::unique_ptr<Weather> DefaultWeather;
    WeatherState WeatherId;
    float Intensity;

    struct LightOverride
    {
        uint32 AreaLightId;
        uint32 OverrideLightId;
        uint32 TransitionMilliseconds;
    };
    std::vector<LightOverride> LightOverrides;
};

#define MIN_UNLOAD_DELAY      1                             // immediate unload
#define MAP_INVALID_ZONE      0xFFFFFFFF

typedef std::map<uint32/*leaderDBGUID*/, CreatureGroup*>        CreatureGroupHolderType;

struct RespawnInfo; // forward declaration
struct CompareRespawnInfo
{
    bool operator()(RespawnInfo const* a, RespawnInfo const* b) const;
};
using ZoneDynamicInfoMap = std::unordered_map<uint32 /*zoneId*/, ZoneDynamicInfo>;
struct RespawnListContainer;
using RespawnInfoMap = std::unordered_map<ObjectGuid::LowType, RespawnInfo*>;
struct TC_GAME_API RespawnInfo
{
    virtual ~RespawnInfo();

    SpawnObjectType type;
    ObjectGuid::LowType spawnId;
    uint32 entry;
    time_t respawnTime;
    uint32 gridId;
};
inline bool CompareRespawnInfo::operator()(RespawnInfo const* a, RespawnInfo const* b) const
{
    if (a == b)
        return false;
    if (a->respawnTime != b->respawnTime)
        return (a->respawnTime > b->respawnTime);
    if (a->spawnId != b->spawnId)
        return a->spawnId < b->spawnId;
    ASSERT(a->type != b->type, "Duplicate respawn entry for spawnId (%u,%u) found!", a->type, a->spawnId);
    return a->type < b->type;
}

struct TC_GAME_API SummonCreatureExtraArgs
{
public:
    SummonCreatureExtraArgs() { }

    SummonCreatureExtraArgs& SetSummonDuration(uint32 duration) { SummonDuration = duration; return *this; }

    SummonPropertiesEntry const* SummonProperties = nullptr;
    Unit* Summoner = nullptr;
    uint32 SummonDuration = 0;
    uint32 SummonSpellId = 0;
    uint32 VehicleRecID = 0;
    uint32 SummonHealth = 0;
    uint32 RideSpell = 0;
    uint8 SeatNumber = 0;
    uint8 CreatureLevel = 0;
    ObjectGuid PrivateObjectOwner;
};

class TC_GAME_API Map : public GridRefManager<NGridType>
{
    friend class MapReference;
    public:
        Map(uint32 id, time_t, uint32 InstanceId, uint8 SpawnMode);
        virtual ~Map();

        MapEntry const* GetEntry() const { return i_mapEntry; }

        // currently unused for normal maps
        bool CanUnload(uint32 diff)
        {
            if (!m_unloadTimer)
                return false;

            if (m_unloadTimer <= diff)
                return true;

            m_unloadTimer -= diff;
            return false;
        }

        virtual bool AddPlayerToMap(Player*);
        virtual void RemovePlayerFromMap(Player*, bool);

        template<class T> bool AddToMap(T *);
        template<class T> void RemoveFromMap(T *, bool);

        void VisitNearbyCellsOf(WorldObject* obj, TypeContainerVisitor<Trinity::ObjectUpdater, GridTypeMapContainer> &gridVisitor, TypeContainerVisitor<Trinity::ObjectUpdater, WorldTypeMapContainer> &worldVisitor);
        virtual void Update(uint32);

        float GetVisibilityRange() const { return m_VisibleDistance; }
        //function for setting up visibility distance for maps on per-type/per-Id basis
        virtual void InitVisibilityDistance();

        void PlayerRelocation(Player*, float x, float y, float z, float orientation);
        void CreatureRelocation(Creature* creature, float x, float y, float z, float ang, bool respawnRelocationOnFail = true);
        void GameObjectRelocation(GameObject* go, float x, float y, float z, float orientation, bool respawnRelocationOnFail = true);
        void DynamicObjectRelocation(DynamicObject* go, float x, float y, float z, float orientation);

        template<class T, class CONTAINER>
        void Visit(Cell const& cell, TypeContainerVisitor<T, CONTAINER> &visitor);

        bool IsRemovalGrid(float x, float y) const
        {
            GridCoord p = Trinity::ComputeGridCoord(x, y);
            return !getNGrid(p.x_coord, p.y_coord) || getNGrid(p.x_coord, p.y_coord)->GetGridState() == GRID_STATE_REMOVAL;
        }
        bool IsRemovalGrid(Position const& pos) const { return IsRemovalGrid(pos.GetPositionX(), pos.GetPositionY()); }

        bool IsGridLoaded(uint32 gridId) const { return IsGridLoaded(GridCoord(gridId % MAX_NUMBER_OF_GRIDS, gridId / MAX_NUMBER_OF_GRIDS)); }
        bool IsGridLoaded(float x, float y) const { return IsGridLoaded(Trinity::ComputeGridCoord(x, y)); }
        bool IsGridLoaded(Position const& pos) const { return IsGridLoaded(pos.GetPositionX(), pos.GetPositionY()); }

        bool GetUnloadLock(GridCoord const& p) const { return getNGrid(p.x_coord, p.y_coord)->getUnloadLock(); }
        void SetUnloadLock(GridCoord const& p, bool on) { getNGrid(p.x_coord, p.y_coord)->setUnloadExplicitLock(on); }
        void LoadGrid(float x, float y);
        void LoadAllCells();
        bool UnloadGrid(NGridType& ngrid, bool pForce);
        void GridMarkNoUnload(uint32 x, uint32 y);
        void GridUnmarkNoUnload(uint32 x, uint32 y);
        virtual void UnloadAll();

        void ResetGridExpiry(NGridType &grid, float factor = 1) const
        {
            grid.ResetTimeTracker(time_t(float(i_gridExpiry)*factor));
        }

        time_t GetGridExpiry(void) const { return i_gridExpiry; }
        uint32 GetId() const;

        static void InitStateMachine();
        static void DeleteStateMachine();

        TerrainInfo* GetTerrain() const { return m_terrain.get(); }

        // custom PathGenerator include and exclude filter flags
        // these modify what kind of terrain types are available in current instance
        // for example this can be used to mark offmesh connections as enabled/disabled
        uint16 GetForceEnabledNavMeshFilterFlags() const { return m_forceEnabledNavMeshFilterFlags; }
        void SetForceEnabledNavMeshFilterFlag(uint16 flag) { m_forceEnabledNavMeshFilterFlags |= flag; }
        void RemoveForceEnabledNavMeshFilterFlag(uint16 flag) { m_forceEnabledNavMeshFilterFlags &= ~flag; }

        uint16 GetForceDisabledNavMeshFilterFlags() const { return m_forceDisabledNavMeshFilterFlags; }
        void SetForceDisabledNavMeshFilterFlag(uint16 flag) { m_forceDisabledNavMeshFilterFlags |= flag; }
        void RemoveForceDisabledNavMeshFilterFlag(uint16 flag) { m_forceDisabledNavMeshFilterFlags &= ~flag; }


        void GetFullTerrainStatusForPosition(PhaseShift const& phaseShift, float x, float y, float z, PositionFullTerrainStatus& data, map_liquidHeaderTypeFlags reqLiquidType = map_liquidHeaderTypeFlags::AllLiquids, float collisionHeight = 2.03128f); // DEFAULT_COLLISION_HEIGHT in Object.h
        ZLiquidStatus GetLiquidStatus(PhaseShift const& phaseShift, float x, float y, float z, map_liquidHeaderTypeFlags ReqLiquidType, LiquidData* data = nullptr, float collisionHeight = 2.03128f); // DEFAULT_COLLISION_HEIGHT in Object.h

        uint32 GetAreaId(PhaseShift const& phaseShift, float x, float y, float z);
        uint32 GetAreaId(PhaseShift const& phaseShift, Position const& pos) {  return GetAreaId(phaseShift, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()); }
        uint32 GetZoneId(PhaseShift const& phaseShift, float x, float y, float z);
        uint32 GetZoneId(PhaseShift const& phaseShift, Position const& pos) { return GetZoneId(phaseShift, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()); }
        void GetZoneAndAreaId(PhaseShift const& phaseShift, uint32& zoneid, uint32& areaid, float x, float y, float z);
        void GetZoneAndAreaId(PhaseShift const& phaseShift, uint32& zoneid, uint32& areaid, Position const& pos) { GetZoneAndAreaId(phaseShift, zoneid, areaid, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()); }

        float GetGridHeight(PhaseShift const& phaseShift, float x, float y);
        float GetMinHeight(PhaseShift const& phaseShift, float x, float y);
        float GetStaticHeight(PhaseShift const& phaseShift, float x, float y, float z, bool checkVMap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH);
        float GetStaticHeight(PhaseShift const& phaseShift, Position const& pos, bool checkVMap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH) { return GetStaticHeight(phaseShift, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), checkVMap, maxSearchDist); }
        float GetHeight(PhaseShift const& phaseShift, float x, float y, float z, bool vmap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH) { return std::max<float>(GetStaticHeight(phaseShift, x, y, z, vmap, maxSearchDist), GetGameObjectFloor(phaseShift, x, y, z, maxSearchDist)); }
        float GetHeight(PhaseShift const& phaseShift, Position const& pos, bool vmap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH) { return GetHeight(phaseShift, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), vmap, maxSearchDist); }

        float GetWaterLevel(PhaseShift const& phaseShift, float x, float y);
        bool IsInWater(PhaseShift const& phaseShift, float x, float y, float z, LiquidData* data = nullptr);
        bool IsUnderWater(PhaseShift const& phaseShift, float x, float y, float z);

        float GetWaterOrGroundLevel(PhaseShift const& phaseShift, float x, float y, float z, float* ground = nullptr, bool swim = false, float collisionHeight = 2.03128f); // DEFAULT_COLLISION_HEIGHT in Object.h

        void MoveAllCreaturesInMoveList();
        void MoveAllGameObjectsInMoveList();
        void MoveAllDynamicObjectsInMoveList();
        void RemoveAllObjectsInRemoveList();
        virtual void RemoveAllPlayers();

        // used only in MoveAllCreaturesInMoveList and ObjectGridUnloader
        bool CreatureRespawnRelocation(Creature* c, bool diffGridOnly);
        bool GameObjectRespawnRelocation(GameObject* go, bool diffGridOnly);

        // assert print helper
        template <typename T>
        static bool CheckGridIntegrity(T* object, bool moved, char const* objType);

        uint32 GetInstanceId() const { return i_InstanceId; }
        uint8 GetSpawnMode() const { return (i_spawnMode); }

        enum EnterState
        {
            CAN_ENTER = 0,
            CANNOT_ENTER_ALREADY_IN_MAP = 1,            // Player is already in the map
            CANNOT_ENTER_NO_ENTRY,                      // No map entry was found for the target map ID
            CANNOT_ENTER_UNINSTANCED_DUNGEON,           // No instance template was found for dungeon map
            CANNOT_ENTER_DIFFICULTY_UNAVAILABLE,        // Requested instance difficulty is not available for target map
            CANNOT_ENTER_NOT_IN_RAID,                   // Target instance is a raid instance and the player is not in a raid group
            CANNOT_ENTER_CORPSE_IN_DIFFERENT_INSTANCE,  // Player is dead and their corpse is not in target instance
            CANNOT_ENTER_INSTANCE_BIND_MISMATCH,        // Player's permanent instance save is not compatible with their group's current instance bind
            CANNOT_ENTER_TOO_MANY_INSTANCES,            // Player has entered too many instances recently
            CANNOT_ENTER_MAX_PLAYERS,                   // Target map already has the maximum number of players allowed
            CANNOT_ENTER_ZONE_IN_COMBAT,                // A boss encounter is currently in progress on the target map
            CANNOT_ENTER_UNSPECIFIED_REASON
        };

        static EnterState PlayerCannotEnter(uint32 mapid, Player* player, bool loginCheck = false);
        virtual EnterState CannotEnter(Player* /*player*/) { return CAN_ENTER; }
        char const* GetMapName() const;

        // have meaning only for instanced map (that have set real difficulty)
        Difficulty GetDifficulty() const { return Difficulty(GetSpawnMode()); }
        bool IsRegularDifficulty() const;
        MapDifficulty const* GetMapDifficulty() const;

        bool Instanceable() const;
        bool IsDungeon() const;
        bool IsNonRaidDungeon() const;
        bool IsRaid() const;
        bool IsRaidOrHeroicDungeon() const;
        bool IsHeroic() const;
        bool Is25ManRaid() const;
        bool IsBattleground() const;
        bool IsBattleArena() const;
        bool IsBattlegroundOrArena() const;
        bool GetEntrancePos(int32& mapid, float& x, float& y) const;

        void AddObjectToRemoveList(WorldObject* obj);
        void AddObjectToSwitchList(WorldObject* obj, bool on);
        virtual void DelayedUpdate(uint32 diff);

        void resetMarkedCells() { marked_cells.reset(); }
        bool isCellMarked(uint32 pCellId) { return marked_cells.test(pCellId); }
        void markCell(uint32 pCellId) { marked_cells.set(pCellId); }

        bool HavePlayers() const { return !m_mapRefManager.isEmpty(); }
        uint32 GetPlayersCountExceptGMs() const;
        bool ActiveObjectsNearGrid(NGridType const& ngrid) const;

        void AddWorldObject(WorldObject* obj) { i_worldObjects.insert(obj); }
        void RemoveWorldObject(WorldObject* obj) { i_worldObjects.erase(obj); }

        void SendToPlayers(WorldPacket const* data) const;

        typedef MapRefManager PlayerList;
        PlayerList const& GetPlayers() const { return m_mapRefManager; }

        template <typename T>
        void DoOnPlayers(T&& fn)
        {
            for (MapReference const& ref : GetPlayers())
                if (Player* player = ref.GetSource())
                    fn(player);
        }

        //per-map script storage
        void ScriptsStart(std::map<uint32, std::multimap<uint32, ScriptInfo>> const& scripts, uint32 id, Object* source, Object* target);
        void ScriptCommandStart(ScriptInfo const& script, uint32 delay, Object* source, Object* target);

        // must called with AddToWorld
        void AddToActive(WorldObject* obj);

        // must called with RemoveFromWorld
        void RemoveFromActive(WorldObject* obj);

        template<class T> void SwitchGridContainers(T* obj, bool on);
        CreatureGroupHolderType CreatureGroupHolder;

        void UpdateIteratorBack(Player* player);

        TempSummon* SummonCreature(uint32 entry, Position const& pos, SummonCreatureExtraArgs const& summonArgs = { });
        void SummonCreatureGroup(uint8 group, std::list<TempSummon*>* list = nullptr);
        Player* GetPlayer(ObjectGuid const& guid);
        AreaTrigger* GetAreaTrigger(ObjectGuid const& guid);
        Corpse* GetCorpse(ObjectGuid const& guid);
        Creature* GetCreature(ObjectGuid const& guid);
        DynamicObject* GetDynamicObject(ObjectGuid const& guid);
        GameObject* GetGameObject(ObjectGuid const& guid);
        Creature* GetCreatureBySpawnId(ObjectGuid::LowType spawnId) const;
        GameObject* GetGameObjectBySpawnId(ObjectGuid::LowType spawnId) const;
        WorldObject* GetWorldObjectBySpawnId(SpawnObjectType type, ObjectGuid::LowType spawnId) const
        {
            switch (type)
            {
                case SPAWN_TYPE_CREATURE:
                    return reinterpret_cast<WorldObject*>(GetCreatureBySpawnId(spawnId));
                case SPAWN_TYPE_GAMEOBJECT:
                    return reinterpret_cast<WorldObject*>(GetGameObjectBySpawnId(spawnId));
                default:
                    return nullptr;
            }
        }
        Pet* GetPet(ObjectGuid const& guid);
        Transport* GetTransport(ObjectGuid const& guid);

        MapStoredObjectTypesContainer& GetObjectsStore() { return _objectsStore; }

        typedef std::unordered_multimap<ObjectGuid::LowType, Creature*> CreatureBySpawnIdContainer;
        CreatureBySpawnIdContainer& GetCreatureBySpawnIdStore() { return _creatureBySpawnIdStore; }
        CreatureBySpawnIdContainer const& GetCreatureBySpawnIdStore() const { return _creatureBySpawnIdStore; }

        typedef std::unordered_multimap<ObjectGuid::LowType, GameObject*> GameObjectBySpawnIdContainer;
        GameObjectBySpawnIdContainer& GetGameObjectBySpawnIdStore() { return _gameObjectBySpawnIdStore; }
        GameObjectBySpawnIdContainer const& GetGameObjectBySpawnIdStore() const { return _gameObjectBySpawnIdStore; }

        std::unordered_set<Corpse*> const* GetCorpsesInCell(uint32 cellId) const
        {
            auto itr = _corpsesByCell.find(cellId);
            if (itr != _corpsesByCell.end())
                return &itr->second;

            return nullptr;
        }

        Corpse* GetCorpseByPlayer(ObjectGuid const& ownerGuid) const
        {
            auto itr = _corpsesByPlayer.find(ownerGuid);
            if (itr != _corpsesByPlayer.end())
                return itr->second;

            return nullptr;
        }

        InstanceMap* ToInstanceMap() { if (IsDungeon()) return reinterpret_cast<InstanceMap*>(this); else return nullptr;  }
        InstanceMap const* ToInstanceMap() const { if (IsDungeon()) return reinterpret_cast<InstanceMap const*>(this); return nullptr; }

        BattlegroundMap* ToBattlegroundMap() { if (IsBattlegroundOrArena()) return reinterpret_cast<BattlegroundMap*>(this); else return nullptr;  }
        BattlegroundMap const* ToBattlegroundMap() const { if (IsBattlegroundOrArena()) return reinterpret_cast<BattlegroundMap const*>(this); return nullptr; }

        bool isInLineOfSight(PhaseShift const& phaseShift, float x1, float y1, float z1, float x2, float y2, float z2, LineOfSightChecks checks, VMAP::ModelIgnoreFlags ignoreFlags) const;
        void Balance() { _dynamicTree.balance(); }
        void RemoveGameObjectModel(const GameObjectModel& model) { _dynamicTree.remove(model); }
        void InsertGameObjectModel(const GameObjectModel& model) { _dynamicTree.insert(model); }
        bool ContainsGameObjectModel(const GameObjectModel& model) const { return _dynamicTree.contains(model);}
        float GetGameObjectFloor(PhaseShift const& phaseShift, float x, float y, float z, float maxSearchDist = DEFAULT_HEIGHT_SEARCH) const
        {
            return _dynamicTree.getHeight(x, y, z, maxSearchDist, phaseShift);
        }
        bool getObjectHitPos(PhaseShift const& phaseShift, float x1, float y1, float z1, float x2, float y2, float z2, float& rx, float &ry, float& rz, float modifyDist);

        virtual uint32 GetOwnerGuildId(uint32 /*team*/ = TEAM_OTHER) const { return 0; }
        /*
            RESPAWN TIMES
        */
        time_t GetLinkedRespawnTime(ObjectGuid guid) const;
        time_t GetRespawnTime(SpawnObjectType type, ObjectGuid::LowType spawnId) const
        {
            auto const& map = GetRespawnMapForType(type);
            auto it = map.find(spawnId);
            return (it == map.end()) ? 0 : it->second->respawnTime;
        }
        time_t GetCreatureRespawnTime(ObjectGuid::LowType spawnId) const { return GetRespawnTime(SPAWN_TYPE_CREATURE, spawnId); }
        time_t GetGORespawnTime(ObjectGuid::LowType spawnId) const { return GetRespawnTime(SPAWN_TYPE_GAMEOBJECT, spawnId); }

        void UpdatePlayerZoneStats(uint32 oldZone, uint32 newZone);

        void SaveRespawnTime(SpawnObjectType type, ObjectGuid::LowType spawnId, uint32 entry, time_t respawnTime, uint32 gridId, CharacterDatabaseTransaction dbTrans = nullptr, bool startup = false);
        void SaveRespawnInfoDB(RespawnInfo const& info, CharacterDatabaseTransaction dbTrans = nullptr);
        void LoadRespawnTimes();
        void DeleteRespawnTimes() { UnloadAllRespawnInfos(); DeleteRespawnTimesInDB(GetId(), GetInstanceId()); }
        static void DeleteRespawnTimesInDB(uint16 mapId, uint32 instanceId);

        void LoadCorpseData();
        void DeleteCorpseData();
        void AddCorpse(Corpse* corpse);
        void RemoveCorpse(Corpse* corpse);
        Corpse* ConvertCorpseToBones(ObjectGuid const& ownerGuid, bool insignia = false);
        void RemoveOldCorpses();

        void SendInitTransports(Player* player);
        void SendRemoveTransports(Player* player);
        void SendUpdateTransportVisibility(Player* player);
        void SendZoneDynamicInfo(uint32 zoneId, Player* player) const;
        void SendZoneWeather(uint32 zoneId, Player* player) const;
        void SendZoneWeather(ZoneDynamicInfo const& zoneDynamicInfo, Player* player) const;


        void SetZoneMusic(uint32 zoneId, uint32 musicId);
        Weather* GetOrGenerateZoneDefaultWeather(uint32 zoneId);
        void SetZoneWeather(uint32 zoneId, WeatherState weatherId, float intensity);
        void SetZoneOverrideLight(uint32 zoneId, uint32 areaLightId, uint32 overrideLightId, uint32 transitionMilliseconds);

        void UpdateAreaDependentAuras();

        template<HighGuid high>
        inline ObjectGuid::LowType GenerateLowGuid()
        {
            static_assert(ObjectGuidTraits<high>::MapSpecific, "Only map specific guid can be generated in Map context");
            return GetGuidSequenceGenerator<high>().Generate();
        }

        template<HighGuid high>
        inline ObjectGuid::LowType GetMaxLowGuid()
        {
            static_assert(ObjectGuidTraits<high>::MapSpecific, "Only map specific guid can be retrieved in Map context");
            return GetGuidSequenceGenerator<high>().GetNextAfterMaxUsed();
        }

        void AddUpdateObject(Object* obj)
        {
            _updateObjects.insert(obj);
        }

        void RemoveUpdateObject(Object* obj)
        {
            _updateObjects.erase(obj);
        }

    private:
        void SetTimer(uint32 t) { i_gridExpiry = t < MIN_GRID_DELAY ? MIN_GRID_DELAY : t; }

        void SendInitSelf(Player* player);

        template <typename T>
        bool MapObjectCellRelocation(T* object, Cell new_cell, char const* objType);

        bool CreatureCellRelocation(Creature* creature, Cell new_cell);
        bool GameObjectCellRelocation(GameObject* go, Cell new_cell);
        bool DynamicObjectCellRelocation(DynamicObject* go, Cell new_cell);

        template<class T> void InitializeObject(T* obj);
        void AddCreatureToMoveList(Creature* c, float x, float y, float z, float ang);
        void RemoveCreatureFromMoveList(Creature* c);
        void AddGameObjectToMoveList(GameObject* go, float x, float y, float z, float ang);
        void RemoveGameObjectFromMoveList(GameObject* go);
        void AddDynamicObjectToMoveList(DynamicObject* go, float x, float y, float z, float ang);
        void RemoveDynamicObjectFromMoveList(DynamicObject* go);

        bool _creatureToMoveLock;
        std::vector<Creature*> _creaturesToMove;

        bool _gameObjectsToMoveLock;
        std::vector<GameObject*> _gameObjectsToMove;

        bool _dynamicObjectsToMoveLock;
        std::vector<DynamicObject*> _dynamicObjectsToMove;

        bool IsGridLoaded(GridCoord const&) const;
        void EnsureGridCreated(GridCoord const&);
        bool EnsureGridLoaded(Cell const&);
        void EnsureGridLoadedForActiveObject(Cell const&, WorldObject* object);

        void buildNGridLinkage(NGridType* pNGridType) { pNGridType->link(this); }

        NGridType* getNGrid(uint32 x, uint32 y) const
        {
            ASSERT(x < MAX_NUMBER_OF_GRIDS && y < MAX_NUMBER_OF_GRIDS, "x = %u, y = %u", x, y);
            return i_grids[x][y];
        }

        bool isGridObjectDataLoaded(uint32 x, uint32 y) const { return getNGrid(x, y)->isGridObjectDataLoaded(); }
        void setGridObjectDataLoaded(bool pLoaded, uint32 x, uint32 y) { getNGrid(x, y)->setGridObjectDataLoaded(pLoaded); }

        void setNGrid(NGridType* grid, uint32 x, uint32 y);
        void ScriptsProcess();

        void SendObjectUpdates();

    protected:

        MapEntry const* i_mapEntry;
        uint8 i_spawnMode;
        uint32 i_InstanceId;
        uint32 m_unloadTimer;
        float m_VisibleDistance;
        DynamicMapTree _dynamicTree;

        MapRefManager m_mapRefManager;
        MapRefManager::iterator m_mapRefIter;

        int32 m_VisibilityNotifyPeriod;

        typedef std::set<WorldObject*> ActiveNonPlayers;
        ActiveNonPlayers m_activeNonPlayers;
        ActiveNonPlayers::iterator m_activeNonPlayersIter;

        // Objects that must update even in inactive grids without activating them
        typedef std::set<Transport*> TransportsContainer;
        TransportsContainer _transports;
        TransportsContainer::iterator _transportsUpdateIter;

    private:
        Player* _GetScriptPlayerSourceOrTarget(Object* source, Object* target, ScriptInfo const* scriptInfo) const;
        Creature* _GetScriptCreatureSourceOrTarget(Object* source, Object* target, ScriptInfo const* scriptInfo, bool bReverse = false) const;
        Unit* _GetScriptUnit(Object* obj, bool isSource, ScriptInfo const* scriptInfo) const;
        Player* _GetScriptPlayer(Object* obj, bool isSource, ScriptInfo const* scriptInfo) const;
        Creature* _GetScriptCreature(Object* obj, bool isSource, ScriptInfo const* scriptInfo) const;
        WorldObject* _GetScriptWorldObject(Object* obj, bool isSource, ScriptInfo const* scriptInfo) const;
        void _ScriptProcessDoor(Object* source, Object* target, ScriptInfo const* scriptInfo) const;
        GameObject* _FindGameObject(WorldObject* pWorldObject, ObjectGuid::LowType guid) const;

        time_t i_gridExpiry;

        std::shared_ptr<TerrainInfo> m_terrain;
        uint16 m_forceEnabledNavMeshFilterFlags;
        uint16 m_forceDisabledNavMeshFilterFlags;

        NGridType* i_grids[MAX_NUMBER_OF_GRIDS][MAX_NUMBER_OF_GRIDS];
        std::bitset<TOTAL_NUMBER_OF_CELLS_PER_MAP*TOTAL_NUMBER_OF_CELLS_PER_MAP> marked_cells;

        //these functions used to process player/mob aggro reactions and
        //visibility calculations. Highly optimized for massive calculations
        void ProcessRelocationNotifies(uint32 diff);

        bool i_scriptLock;
        std::set<WorldObject*> i_objectsToRemove;
        std::map<WorldObject*, bool> i_objectsToSwitch;
        std::set<WorldObject*> i_worldObjects;

        typedef std::multimap<time_t, ScriptAction> ScriptScheduleMap;
        ScriptScheduleMap m_scriptSchedule;

    public:
        void ProcessRespawns();
        void ApplyDynamicModeRespawnScaling(WorldObject const* obj, ObjectGuid::LowType spawnId, uint32& respawnDelay, uint32 mode) const;

    private:
        // if return value is true, we can respawn
        // if return value is false, reschedule the respawn to new value of info->respawnTime iff nonzero, delete otherwise
        // if return value is false and info->respawnTime is nonzero, it is guaranteed to be greater than time(NULL)
        bool CheckRespawn(RespawnInfo* info);
        void DoRespawn(SpawnObjectType type, ObjectGuid::LowType spawnId, uint32 gridId);
        bool AddRespawnInfo(RespawnInfo const& info);
        void UnloadAllRespawnInfos();
        RespawnInfo* GetRespawnInfo(SpawnObjectType type, ObjectGuid::LowType spawnId) const;
        void Respawn(RespawnInfo* info, CharacterDatabaseTransaction dbTrans = nullptr);
        void DeleteRespawnInfo(RespawnInfo* info, CharacterDatabaseTransaction dbTrans = nullptr);
        void DeleteRespawnInfoFromDB(SpawnObjectType type, ObjectGuid::LowType spawnId, CharacterDatabaseTransaction dbTrans = nullptr);

    public:
        void GetRespawnInfo(std::vector<RespawnInfo const*>& respawnData, SpawnObjectTypeMask types) const;
        void Respawn(SpawnObjectType type, ObjectGuid::LowType spawnId, CharacterDatabaseTransaction dbTrans = nullptr)
        {
            if (RespawnInfo* info = GetRespawnInfo(type, spawnId))
                Respawn(info, dbTrans);
        }
        void RemoveRespawnTime(SpawnObjectType type, ObjectGuid::LowType spawnId, CharacterDatabaseTransaction dbTrans = nullptr, bool alwaysDeleteFromDB = false)
        {
            if (RespawnInfo* info = GetRespawnInfo(type, spawnId))
                DeleteRespawnInfo(info, dbTrans);
            // Some callers might need to make sure the database doesn't contain any respawn time
            else if (alwaysDeleteFromDB)
                DeleteRespawnInfoFromDB(type, spawnId, dbTrans);
        }
        size_t DespawnAll(SpawnObjectType type, ObjectGuid::LowType spawnId);

        bool ShouldBeSpawnedOnGridLoad(SpawnObjectType type, ObjectGuid::LowType spawnId) const;
        template <typename T> bool ShouldBeSpawnedOnGridLoad(ObjectGuid::LowType spawnId) const { return ShouldBeSpawnedOnGridLoad(SpawnData::TypeFor<T>, spawnId); }

        SpawnGroupTemplateData const* GetSpawnGroupData(uint32 groupId) const;

        bool IsSpawnGroupActive(uint32 groupId) const;

        // Enable the spawn group, which causes all creatures in it to respawn (unless they have a respawn timer)
        // The force flag can be used to force spawning additional copies even if old copies are still around from a previous spawn
        bool SpawnGroupSpawn(uint32 groupId, bool ignoreRespawn = false, bool force = false, std::vector<WorldObject*>* spawnedObjects = nullptr);

        // Despawn all creatures in the spawn group if spawned, optionally delete their respawn timer, and disable the group
        bool SpawnGroupDespawn(uint32 groupId, bool deleteRespawnTimes = false, size_t* count = nullptr);

        // Disable the spawn group, which prevents any creatures in the group from respawning until re-enabled
        // This will not affect any already-present creatures in the group
        void SetSpawnGroupInactive(uint32 groupId) { SetSpawnGroupActive(groupId, false); }

        SpawnedPoolData& GetPoolData() { return *_poolData; }
        SpawnedPoolData const& GetPoolData() const { return *_poolData; }

        typedef std::function<void(Map*)> FarSpellCallback;
        void AddFarSpellCallback(FarSpellCallback&& callback);

    private:
        // Type specific code for add/remove to/from grid
        template<class T>
        void AddToGrid(T* object, Cell const& cell);

        template<class T>
        void DeleteFromWorld(T*);

        void AddToActiveHelper(WorldObject* obj)
        {
            m_activeNonPlayers.insert(obj);
        }

        void RemoveFromActiveHelper(WorldObject* obj)
        {
            // Map::Update for active object in proccess
            if (m_activeNonPlayersIter != m_activeNonPlayers.end())
            {
                ActiveNonPlayers::iterator itr = m_activeNonPlayers.find(obj);
                if (itr == m_activeNonPlayers.end())
                    return;
                if (itr == m_activeNonPlayersIter)
                    ++m_activeNonPlayersIter;
                m_activeNonPlayers.erase(itr);
            }
            else
                m_activeNonPlayers.erase(obj);
        }

        std::unique_ptr<RespawnListContainer> _respawnTimes;
        RespawnInfoMap       _creatureRespawnTimesBySpawnId;
        RespawnInfoMap       _gameObjectRespawnTimesBySpawnId;
        RespawnInfoMap& GetRespawnMapForType(SpawnObjectType type)
        {
            switch (type)
            {
                default:
                    ASSERT(false);
                case SPAWN_TYPE_CREATURE:
                    return _creatureRespawnTimesBySpawnId;
                case SPAWN_TYPE_GAMEOBJECT:
                    return _gameObjectRespawnTimesBySpawnId;
            }
        }
        RespawnInfoMap const& GetRespawnMapForType(SpawnObjectType type) const
        {
            switch (type)
            {
                default:
                    ASSERT(false);
                case SPAWN_TYPE_CREATURE:
                    return _creatureRespawnTimesBySpawnId;
                case SPAWN_TYPE_GAMEOBJECT:
                    return _gameObjectRespawnTimesBySpawnId;
            }
        }

        void SetSpawnGroupActive(uint32 groupId, bool state);
        void UpdateSpawnGroupConditions();
        std::unordered_set<uint32> _toggledSpawnGroupIds;

        uint32 _respawnCheckTimer;
        std::unordered_map<uint32, uint32> _zonePlayerCountMap;

        ZoneDynamicInfoMap _zoneDynamicInfo;
        IntervalTimer _weatherUpdateTimer;

        template<HighGuid high>
        inline ObjectGuidGeneratorBase& GetGuidSequenceGenerator()
        {
            auto itr = _guidGenerators.find(high);
            if (itr == _guidGenerators.end())
                itr = _guidGenerators.insert(std::make_pair(high, std::unique_ptr<ObjectGuidGenerator<high>>(new ObjectGuidGenerator<high>()))).first;

            return *itr->second;
        }

        std::map<HighGuid, std::unique_ptr<ObjectGuidGeneratorBase>> _guidGenerators;
        std::unique_ptr<SpawnedPoolData> _poolData;
        MapStoredObjectTypesContainer _objectsStore;
        CreatureBySpawnIdContainer _creatureBySpawnIdStore;
        GameObjectBySpawnIdContainer _gameObjectBySpawnIdStore;
        std::unordered_map<uint32/*cellId*/, std::unordered_set<Corpse*>> _corpsesByCell;
        std::unordered_map<ObjectGuid, Corpse*> _corpsesByPlayer;
        std::unordered_set<Corpse*> _corpseBones;

        std::unordered_set<Object*> _updateObjects;

        MPSCQueue<FarSpellCallback> _farSpellCallbacks;

        /*********************************************************/
        /***                   WorldStates                     ***/
        /*********************************************************/
    public:
        int32 GetWorldStateValue(int32 worldStateId) const;
        void SetWorldStateValue(int32 worldStateId, int32 value, bool hidden);
        WorldStateValueContainer const& GetWorldStateValues() const { return _worldStateValues; }

    private:
        WorldStateValueContainer _worldStateValues;
};

enum InstanceResetMethod
{
    INSTANCE_RESET_ALL,
    INSTANCE_RESET_CHANGE_DIFFICULTY,
    INSTANCE_RESET_GLOBAL,
    INSTANCE_RESET_GROUP_DISBAND,
    INSTANCE_RESET_GROUP_JOIN,
    INSTANCE_RESET_RESPAWN_DELAY
};

class TC_GAME_API InstanceMap : public Map
{
    public:
        InstanceMap(uint32 id, time_t, uint32 InstanceId, uint8 SpawnMode, TeamId InstanceTeam);
        ~InstanceMap();
        bool AddPlayerToMap(Player*) override;
        void RemovePlayerFromMap(Player*, bool) override;
        void Update(uint32) override;
        void CreateInstanceData(bool load);
        bool Reset(uint8 method);
        uint32 GetScriptId() const { return i_script_id; }
        std::string const& GetScriptName() const;
        InstanceScript* GetInstanceScript() { return i_data; }
        InstanceScript const* GetInstanceScript() const { return i_data; }
        void PermBindAllPlayers();
        void UnloadAll() override;
        EnterState CannotEnter(Player* player) override;
        void SendResetWarnings(uint32 timeLeft) const;
        void SetResetSchedule(bool on);

        /* this checks if any players have a permanent bind (included reactivatable expired binds) to the instance ID
        it needs a DB query, so use sparingly */
        bool HasPermBoundPlayers() const;
        uint32 GetMaxPlayers() const;
        uint32 GetMaxResetDelay() const;
        TeamId GetTeamIdInInstance() const;
        Team GetTeamInInstance() const { return GetTeamIdInInstance() == TEAM_ALLIANCE ? ALLIANCE : HORDE; }

        virtual void InitVisibilityDistance() override;
    private:
        bool m_resetAfterUnload;
        bool m_unloadWhenEmpty;
        InstanceScript* i_data;
        uint32 i_script_id;
};

class TC_GAME_API BattlegroundMap : public Map
{
    public:
        BattlegroundMap(uint32 id, time_t, uint32 InstanceId, uint8 spawnMode);
        ~BattlegroundMap();

        bool AddPlayerToMap(Player*) override;
        void RemovePlayerFromMap(Player*, bool) override;
        EnterState CannotEnter(Player* player) override;
        void SetUnload();
        //void UnloadAll(bool pForce);
        void RemoveAllPlayers() override;

        virtual void InitVisibilityDistance() override;
        Battleground* GetBG() { return m_bg; }
        void SetBG(Battleground* bg) { m_bg = bg; }
    private:
        Battleground* m_bg;
};

template<class T, class CONTAINER>
inline void Map::Visit(Cell const& cell, TypeContainerVisitor<T, CONTAINER>& visitor)
{
    const uint32 x = cell.GridX();
    const uint32 y = cell.GridY();
    const uint32 cell_x = cell.CellX();
    const uint32 cell_y = cell.CellY();

    if (!cell.NoCreate() || IsGridLoaded(GridCoord(x, y)))
    {
        EnsureGridLoaded(cell);
        getNGrid(x, y)->VisitGrid(cell_x, cell_y, visitor);
    }
}
#endif
