#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <fstream>
#include <map>
#include <algorithm>

// ---------- Soil Structure ----------
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

// ---------- Plant Structure ----------
struct Plant {
    int id;
    Vector3 position;
    float size;
    Color color;
    float growthRate;
    float health;
    float age;
    float maxAge;
    bool alive;

    Plant(int _id, Vector3 pos, float s, float rate)
        : id(_id), position(pos), size(s), growthRate(rate), alive(true) {
        health = 1.0f; age = 0.0f;
        maxAge = 80.0f + (rand() % 40);
        color = {50,150,50,255};
    }

    std::vector<int> getOccupiedSoilIndices(int gridSize, float cellSize) {
        std::vector<int> indices;
        int minX = (int)((position.x - size/2 + gridSize/2) / cellSize);
        int maxX = (int)((position.x + size/2 + gridSize/2) / cellSize);
        int minZ = (int)((position.z - size/2 + gridSize/2) / cellSize);
        int maxZ = (int)((position.z + size/2 + gridSize/2) / cellSize);

        for(int x = minX; x <= maxX; x++)
            for(int z = minZ; z <= maxZ; z++)
                if(x >= 0 && x < gridSize && z >= 0 && z < gridSize)
                    indices.push_back(z*gridSize + x);

        return indices;
    }

    float calculateLight(const std::vector<Plant>& allPlants) {
        float light = 1.0f;
        for(auto &other : allPlants) {
            if(other.id == id) continue;
            if(position.x + size/2 > other.position.x - other.size/2 &&
               position.x - size/2 < other.position.x + other.size/2 &&
               position.z + size/2 > other.position.z - other.size/2 &&
               position.z - size/2 < other.position.z + other.size/2 &&
               position.y < other.position.y + other.size/2) {
                   light *= 0.8f;
            }
        }
        return light;
    }

    void grow(std::vector<SoilCell> &soil, int gridSize, float cellSize, const std::vector<Plant>& allPlants,
              std::map<int,std::map<int,float>> &nutrientUsed,
              std::map<int,std::vector<std::pair<int,float>>> &cellUsage,
              float HEIGHT_SCALE) {

        if(!alive) return;

        float lightFactor = calculateLight(allPlants);
        auto indices = getOccupiedSoilIndices(gridSize, cellSize);

        float nutrientFactor = 0.0f, waterFactor = 0.0f;
        for(int idx : indices) {
            nutrientFactor += (soil[idx].nitrogen + soil[idx].phosphorus + soil[idx].potassium)/3.0f;
            waterFactor += soil[idx].water;
        }
        nutrientFactor /= indices.size();
        waterFactor /= indices.size();

        float agePerc = age / maxAge;
        float delta = growthRate * lightFactor * nutrientFactor * waterFactor * 0.02f;

        if(agePerc > 0.25f && agePerc < 0.3f) delta = 0;
        if(agePerc > 0.3f) delta *= 0.5f;
        if(agePerc > 0.6f) delta *= 0.3f;

        if(delta > 0.05f) delta = 0.05f;

        size += delta;
        if(agePerc > 0.6f) size -= 0.001f;
        if(size < 0.1f) size = 0.1f;

        float deltaPerCell = delta / indices.size();
        for(int idx : indices) {
            soil[idx].water = std::max(0.0f, soil[idx].water - deltaPerCell*0.001f);
            soil[idx].nitrogen = std::max(0.0f, soil[idx].nitrogen - deltaPerCell*0.001f);
            soil[idx].phosphorus = std::max(0.0f, soil[idx].phosphorus - deltaPerCell*0.001f);
            soil[idx].potassium = std::max(0.0f, soil[idx].potassium - deltaPerCell*0.001f);

            nutrientUsed[id][idx] = deltaPerCell;
            cellUsage[idx].push_back({id, deltaPerCell});
        }

        health -= delta * 0.005f;
        health -= (1.0f - nutrientFactor * waterFactor) * 0.0005f;
        if(health < 0) health = 0;

        age += 0.01f;

        if(agePerc > 0.6f) color.a = (unsigned char)((1.0f - (agePerc-0.6f)/0.4f) * 255);
        else color.a = 255;

        if(agePerc < 0.5f) {
            float driftX = ((rand()%100)/50000.0f - 0.001f);
            float driftZ = ((rand()%100)/50000.0f - 0.001f);
            if(driftX > 0) position.x += driftX;
            if(driftZ > 0) position.z += driftZ;
        }

        float halfGrid = gridSize / 2.0f;
        position.x = std::max(-halfGrid, std::min(position.x, halfGrid));
        position.z = std::max(-halfGrid, std::min(position.z, halfGrid));

        position.y = size * HEIGHT_SCALE / 2.0f + 0.1f;

        alive = (health > 0.0f && age < maxAge);
    }
};

// ---------- Main Program ----------
int main() {
    const int screenWidth = 1200, screenHeight = 800;
    const int GRID_SIZE = 40;
    const float CELL_SIZE = 1.0f;
    const int NUM_PLANTS = 60;
    const float HEIGHT_SCALE = 5.0f;

    InitWindow(screenWidth, screenHeight, "3D Plant-Soil Ecosystem");
    SetTargetFPS(60);

    Camera3D camera = {0};
    camera.position = {30, 20, 30};
    camera.target = {0, 0, 0};
    camera.up = {0,1,0};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    srand(time(0));

    std::vector<SoilCell> soil;
    for(int z=0; z<GRID_SIZE; ++z)
        for(int x=0; x<GRID_SIZE; ++x)
            soil.push_back(SoilCell({(float)x,(float)z}));

    std::vector<Plant> plants;
    for(int i=0; i<NUM_PLANTS; ++i) {
        Vector3 pos = {(float)(rand()%GRID_SIZE - GRID_SIZE/2), 0.5f, (float)(rand()%GRID_SIZE - GRID_SIZE/2)};
        float growthRate = 0.02f + (rand()%10)/1000.0f;
        plants.push_back(Plant(i, pos, 1.0f, growthRate));
    }

    std::ofstream plantLog("plant_growth.csv");
    plantLog << "Frame,PlantID,X,Y,Z,Age,Size,Health,Alive\n";

    std::ofstream soilLog("soil_status.csv");
    soilLog << "Frame,SoilX,SoilZ,Water,Nitrogen,Phosphorus,Potassium,Occupancy,PlantUsage\n";

    int frame = 0;

    while(!WindowShouldClose()) {
        float speed = 10.0f * GetFrameTime();
        if(IsKeyDown(KEY_W)) camera.position = Vector3Add(camera.position, (Vector3){0,0,-speed});
        if(IsKeyDown(KEY_S)) camera.position = Vector3Add(camera.position, (Vector3){0,0,speed});
        if(IsKeyDown(KEY_A)) camera.position = Vector3Add(camera.position, (Vector3){-speed,0,0});
        if(IsKeyDown(KEY_D)) camera.position = Vector3Add(camera.position, (Vector3){speed,0,0});
        if(IsKeyDown(KEY_SPACE)) camera.position = Vector3Add(camera.position, (Vector3){0,speed,0});
        if(IsKeyDown(KEY_LEFT_CONTROL)) camera.position = Vector3Add(camera.position, (Vector3){0,-speed,0});

        camera.target = (Vector3){0,0,0};

        std::map<int,std::map<int,float>> nutrientUsed;
        std::map<int,std::vector<std::pair<int,float>>> cellUsage;

        for(auto &p : plants)
            p.grow(soil, GRID_SIZE, CELL_SIZE, plants, nutrientUsed, cellUsage, HEIGHT_SCALE);

        BeginDrawing();
            ClearBackground(RAYWHITE);
            BeginMode3D(camera);

                for(auto &s : soil)
                    DrawCube({s.position.x - GRID_SIZE/2 + 0.5f, 0, s.position.y - GRID_SIZE/2 + 0.5f},
                             CELL_SIZE, 0.2f, CELL_SIZE, (Color){139,69,19,255});

                for(auto &p : plants)
                    if(p.alive) {
                        DrawCube(p.position, p.size, p.size, p.size, p.color);
                        DrawCubeWires(p.position, p.size, p.size, p.size, BLACK);
                    }

            EndMode3D();

            DrawText("Use WASD + Space/CTRL to move.",10,10,20,DARKGRAY);
            DrawText(TextFormat("Plants alive: %d", (int)std::count_if(plants.begin(), plants.end(),
                        [](Plant &p){ return p.alive; })),10,40,20,DARKGREEN);

        EndDrawing();

        for(auto &p : plants)
            plantLog << frame << "," << p.id << "," << p.position.x << "," << p.position.y << "," << p.position.z << ","
                     << p.age << "," << p.size << "," << p.health << "," << p.alive << "\n";

        for(auto &[idx, usageList] : cellUsage) {
            int x = (int)(soil[idx].position.x);
            int z = (int)(soil[idx].position.y);
            soilLog << frame << "," << x << "," << z << "," << soil[idx].water << "," << soil[idx].nitrogen << ","
                    << soil[idx].phosphorus << "," << soil[idx].potassium << "," << usageList.size();

            for(auto &[plantId, amount] : usageList)
                soilLog << "," << plantId << ":" << amount;
            soilLog << "\n";
        }

        frame++;
    }

    plantLog.close();
    soilLog.close();
    CloseWindow();
    return 0;
}
