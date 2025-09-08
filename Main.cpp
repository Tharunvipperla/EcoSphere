#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <fstream>
#include <map>
#include <algorithm>

static inline float clampf(float v, float a, float b){ return std::max(a, std::min(v,b)); }
static inline int safeFloorToInt(float v){ return (int)std::floor(v); }

float overlapAreaXZ(float ax, float az, float ah, float bx, float bz, float bh) {
    float aMinX = ax - ah, aMaxX = ax + ah;
    float aMinZ = az - ah, aMaxZ = az + ah;
    float bMinX = bx - bh, bMaxX = bx + bh;
    float bMinZ = bz - bh, bMaxZ = bz + bh;

    float ix = std::max(0.0f, std::min(aMaxX, bMaxX) - std::max(aMinX, bMinX));
    float iz = std::max(0.0f, std::min(aMaxZ, bMaxZ) - std::max(aMinZ, bMinZ));
    return ix * iz;
}

// ---------- Soil ----------
struct SoilCell {
    Vector2 position; // X,Z
    float water, nitrogen, phosphorus, potassium;
    SoilCell(Vector2 pos) : position(pos) {
        water = 0.5f + (rand() % 50)/100.0f;
        nitrogen = 0.5f + (rand() % 50)/100.0f;
        phosphorus = 0.5f + (rand() % 50)/100.0f;
        potassium = 0.5f + (rand() % 50)/100.0f;
    }
};

// ---------- Plant ----------
struct Plant {
    int id;
    Vector3 position;
    float size;
    Color color;
    float growthRate;
    float health;
    float age, maxAge;
    bool alive;

    float photosyntheticEfficiency;
    float baseMaintenance;
    float maintenancePerSize;
    float adsorptionEfficiency;

    // per-frame debug stats
    float lastNutrientIntake;
    float lastAreaOccupied;

    Plant(int _id, Vector3 pos, float s, float rate)
        : id(_id), position(pos), size(s), growthRate(rate), alive(true) {
        health = 1.0f; age = 0.0f;
        maxAge = 200.0f + (rand() % 100); // slower life
        color = {50,150,50,255};

        photosyntheticEfficiency = 1.0f;
        baseMaintenance = 0.2f;
        maintenancePerSize = 0.01f;
        adsorptionEfficiency = 0.9f;

        lastNutrientIntake = 0.0f;
        lastAreaOccupied = 0.0f;
    }

    std::vector<int> getOccupiedSoilIndices(int gridSize, float cellSize) const {
        std::vector<int> indices;
        float halfGrid = gridSize * 0.5f;
        int minX = safeFloorToInt((position.x - size*0.5f + halfGrid) / cellSize);
        int maxX = safeFloorToInt((position.x + size*0.5f + halfGrid) / cellSize);
        int minZ = safeFloorToInt((position.z - size*0.5f + halfGrid) / cellSize);
        int maxZ = safeFloorToInt((position.z + size*0.5f + halfGrid) / cellSize);

        for(int x = minX; x <= maxX; ++x)
            for(int z = minZ; z <= maxZ; ++z)
                if(x >= 0 && x < gridSize && z >= 0 && z < gridSize)
                    indices.push_back(z*gridSize + x);
        return indices;
    }

    float overlapFractionWithCell(const SoilCell &cell, int gridSize, float cellSize) const {
        float halfGrid = gridSize * 0.5f;
        float cx = cell.position.x - halfGrid + cellSize*0.5f;
        float cz = cell.position.y - halfGrid + cellSize*0.5f;
        float plantHalf = size * 0.5f;
        float cellHalf = cellSize * 0.5f;
        float overlap = overlapAreaXZ(position.x, position.z, plantHalf, cx, cz, cellHalf);
        float cellArea = cellSize * cellSize;
        return clampf(overlap / cellArea, 0.0f, 1.0f);
    }

    float topY() const { return position.y + (size*0.5f); }

    float calculateLight(const std::vector<Plant>& allPlants) const {
        float light = 1.0f;
        float plantHalf = size*0.5f;
        for(const auto &other : allPlants){
            if(other.id == id) continue;
            float overlap = overlapAreaXZ(position.x, position.z, plantHalf,
                                          other.position.x, other.position.z, other.size*0.5f);
            if(overlap <= 0) continue;
            float overlapFrac = overlap / (size*size);
            if(other.topY() > topY()) {
                light *= (1.0f - overlapFrac*0.5f); // softer shading
            }
        }
        return clampf(light,0,1);
    }

    void grow(std::vector<SoilCell> &soil, int gridSize, float cellSize,
              float lightFactor,
              std::map<int,std::map<int,float>> &nutrientUsed,
              std::map<int,std::vector<std::tuple<int,float,float>>> &cellUsage,
              float HEIGHT_SCALE) {

        if(!alive) return;
        lastNutrientIntake = 0.0f;
        lastAreaOccupied = 0.0f;

        auto indices = getOccupiedSoilIndices(gridSize, cellSize);
        float totalOverlap = 0;
        float nutrientSum = 0, waterSum = 0;
        std::vector<std::pair<int,float>> overlaps;
        for(int idx: indices){
            float frac = overlapFractionWithCell(soil[idx], gridSize, cellSize);
            if(frac>0){
                overlaps.push_back({idx,frac});
                totalOverlap += frac;
                float avgNPK = (soil[idx].nitrogen+soil[idx].phosphorus+soil[idx].potassium)/3.0f;
                nutrientSum += avgNPK*frac;
                waterSum += soil[idx].water*frac;
            }
        }
        if(totalOverlap==0){ age+=0.01f; return; }

        float nutrientFactor = nutrientSum/totalOverlap;
        float waterFactor = waterSum/totalOverlap;
        float agePerc = age/maxAge;

        float production = lightFactor * nutrientFactor * waterFactor *
                           photosyntheticEfficiency * growthRate;
        float maintenance = baseMaintenance + maintenancePerSize*size + agePerc*0.2f;
        float netEnergy = production - maintenance;

        float delta = growthRate*0.01f * (netEnergy>0?1:0.2f);
        size += delta;
        size = std::max(0.2f, size);

        float demand = std::max(0.0f,delta*0.5f);
        for(auto &pr: overlaps){
            int idx = pr.first;
            float frac = pr.second;
            float takeFrac = frac/totalOverlap;
            float amountTaken = demand*takeFrac*adsorptionEfficiency;
            soil[idx].water = std::max(0.0f, soil[idx].water - amountTaken*0.001f);
            soil[idx].nitrogen = std::max(0.0f, soil[idx].nitrogen - amountTaken*0.001f);
            soil[idx].phosphorus = std::max(0.0f, soil[idx].phosphorus - amountTaken*0.001f);
            soil[idx].potassium = std::max(0.0f, soil[idx].potassium - amountTaken*0.001f);

            nutrientUsed[id][idx]+=amountTaken;
            cellUsage[idx].push_back({id, frac, amountTaken});

            lastNutrientIntake += amountTaken;
            lastAreaOccupied += frac;
        }

        health += netEnergy*0.001f - 0.0005f;
        health = clampf(health,0,1);
        age+=0.01f;
        position.y = size*HEIGHT_SCALE/2.0f+0.1f;

        if(agePerc<0.6f){ color={50,150,50,255}; }
        else { color={200,200,80,(unsigned char)(255*(1.0f-(agePerc-0.6f)/0.4f))}; }

        alive=(health>0 && age<maxAge);
    }
};

// ---------- Main ----------
int main(){
    const int screenWidth=1200, screenHeight=800;
    const int GRID_SIZE=40;
    const float CELL_SIZE=1.0f;
    const int NUM_PLANTS=40;
    const float HEIGHT_SCALE=3.0f;

    InitWindow(screenWidth,screenHeight,"Ecosystem");
    SetTargetFPS(60);

    Camera3D camera={0};
    camera.position={30,20,30}; camera.target={0,0,0};
    camera.up={0,1,0}; camera.fovy=45; camera.projection=CAMERA_PERSPECTIVE;

    srand((unsigned)time(0));
    std::vector<SoilCell> soil;
    for(int z=0;z<GRID_SIZE;z++) for(int x=0;x<GRID_SIZE;x++)
        soil.push_back(SoilCell({(float)x,(float)z}));

    std::vector<Plant> plants;
    for(int i=0;i<NUM_PLANTS;i++){
        Vector3 pos={(float)(rand()%GRID_SIZE-GRID_SIZE/2),0.5f,
                     (float)(rand()%GRID_SIZE-GRID_SIZE/2)};
        float growthRate=0.01f+(rand()%10)/2000.0f;
        plants.push_back(Plant(i,pos,1.0f,growthRate));
    }

    std::ofstream plantLog("plant_growth.csv");
    plantLog<<"Frame,PlantID,X,Y,Z,Age,Size,Health,Alive,NutrientIntake,AreaOccupied\n";

    std::ofstream soilLog("soil_status.csv");
    soilLog<<"Frame,SoilX,SoilZ,Water,Nitrogen,Phosphorus,Potassium,Occupancy,PlantUsage,PlantOverlap,PlantNutrientIntake\n";

    int frame=0;
    while(!WindowShouldClose()){
        float speed=10*GetFrameTime();
        if(IsKeyDown(KEY_W)) camera.position=Vector3Add(camera.position,{0,0,-speed});
        if(IsKeyDown(KEY_S)) camera.position=Vector3Add(camera.position,{0,0,speed});
        if(IsKeyDown(KEY_A)) camera.position=Vector3Add(camera.position,{-speed,0,0});
        if(IsKeyDown(KEY_D)) camera.position=Vector3Add(camera.position,{speed,0,0});
        if(IsKeyDown(KEY_SPACE)) camera.position=Vector3Add(camera.position,{0,speed,0});
        if(IsKeyDown(KEY_LEFT_CONTROL)) camera.position=Vector3Add(camera.position,{0,-speed,0});
        camera.target={0,0,0};

        std::vector<float> lightFactors(plants.size());
        for(size_t i=0;i<plants.size();i++)
            lightFactors[i]=plants[i].calculateLight(plants);

        std::map<int,std::map<int,float>> nutrientUsed;
        std::map<int,std::vector<std::tuple<int,float,float>>> cellUsage;

        for(size_t i=0;i<plants.size();i++)
            plants[i].grow(soil,GRID_SIZE,CELL_SIZE,lightFactors[i],
                           nutrientUsed,cellUsage,HEIGHT_SCALE);

        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);
        for(auto &s:soil)
            DrawCube({s.position.x-GRID_SIZE/2+0.5f,0,
                      s.position.y-GRID_SIZE/2+0.5f},
                      CELL_SIZE,0.2f,CELL_SIZE,(Color){139,69,19,255});
        for(auto &p:plants)
            if(p.alive){
                DrawCube(p.position,p.size,p.size*HEIGHT_SCALE,p.size,p.color);
                DrawCubeWires(p.position,p.size,p.size*HEIGHT_SCALE,p.size,BLACK);
            }
        EndMode3D();
        DrawText("WASD+Space/CTRL",10,10,20,DARKGRAY);
        EndDrawing();

        for(auto &p:plants)
            plantLog<<frame<<","<<p.id<<","<<p.position.x<<","<<p.position.y<<","<<p.position.z
                    <<","<<p.age<<","<<p.size<<","<<p.health<<","<<p.alive
                    <<","<<p.lastNutrientIntake<<","<<p.lastAreaOccupied<<"\n";

        for(auto &[idx,usageList]:cellUsage){
            int x=(int)soil[idx].position.x, z=(int)soil[idx].position.y;
            soilLog<<frame<<","<<x<<","<<z<<","<<soil[idx].water<<","<<soil[idx].nitrogen
                   <<","<<soil[idx].phosphorus<<","<<soil[idx].potassium<<","<<usageList.size();

            soilLog<<",\""; for(auto &t:usageList) soilLog<<std::get<0>(t)<<":"<<std::get<2>(t)<<" "; soilLog<<"\"";
            soilLog<<",\""; for(auto &t:usageList) soilLog<<std::get<0>(t)<<":"<<std::get<1>(t)<<" "; soilLog<<"\"";
            soilLog<<",\""; for(auto &t:usageList) soilLog<<std::get<0>(t)<<":"<<std::get<2>(t)<<" "; soilLog<<"\"";
            soilLog<<"\n";
        }
        frame++;
    }
    plantLog.close(); soilLog.close(); CloseWindow();
    return 0;
}
