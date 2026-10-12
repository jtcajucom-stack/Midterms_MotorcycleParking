#define GL_SILENCE_DEPRECATION

#ifdef __APPLE__
#include <GLUT/glut.h>

#else
#include <GL/glut.h>
#endif

#include <ctime>
#include <cmath>
#include <iostream>
#include <string>
#include <cstdlib>
#include <algorithm>
using namespace std;

/*
Name: Cajucom, Juztin Eaivenhazer T.
*/

// H toggles the HUD; the rest of the game remains visible.
bool showHUD = true;

/* RestorePoint 1.3.4 - Sidecar Food Carts, Depth Layers, Smooth Movement */
int lastCollisionTime = -10000;
const int COLLISION_COOLDOWN_MS = 1000;
int healthPoints = 3;
const int MAX_HEALTH = 3;
bool gameOver = false;

// Score and level progression
int score = 0;
int level = 1;

// Green parking zone
float parkingX = 0.0f;
float parkingY = 0.0f;
const float PARK_HALF_W = 0.26f;
const float PARK_HALF_H = 0.48f;

// Slot dimensions shared by parking markings and procedural generation.
const int PARKING_COLS = 9;
const int PARKING_ROWS = 3;
const float PARKING_SLOT_SPACING_X = 0.52f;

// Row spacing tightens with progression, but never below a safe maneuvering limit.
float parkingRowSpacing = 1.85f;

// Vertical position adjustment for the entire parking layout
const float PARKING_LAYOUT_OFFSET_Y = 0.7f;

float parkingRowY(int row)
{
    return (row - (PARKING_ROWS - 1) / 2.0f)
        * parkingRowSpacing
        + PARKING_LAYOUT_OFFSET_Y;
}

void updateParkingRowSpacing()
{
    // Rows move closer together as levels advance, then stop at 1.25 units.
    // Cars are about 0.70 units long, leaving a 0.55-unit aisle at the
    // tightest setting. This is narrow, but wide enough for the motorcycle
    // to pass sideways when aligned carefully.
    parkingRowSpacing = max(1.25f, 1.85f - 0.10f * (level - 1));
}

float parkingSlotX(int col)
{
    return (col - (PARKING_COLS - 1) / 2.0f) * PARKING_SLOT_SPACING_X;
}

// Parking feedback
bool parkingAttemptFailed = false;

// Function declarations
void generateLevel();
void generatePuzzleCars();
void completeParkingLevel();
bool isParkingSuccessful();
void drawParkingZone();
void generateTrafficObstacles();
void placePlayerAtRandomSpawn();
void drawTrafficMotorcycle(int index);
void drawFoodCart(int index);
void specialKeyUp(int key, int x, int y);
void updateMovement(int value);

// Multiple parked cars
const int MAX_CARS = 30;
int carCount = 6;

// Each car has an X and Y position
float carPositions[MAX_CARS][2] = {
    { -3.5f, 2.2f },
    { -2.5f,   2.2f },
    { -1.0f,   0.5f },
    { 2.0f,   1.0f },
    { 0.0f,   0.0f },
    { 3.0f, 2.0f }
};
float carAngles[MAX_CARS] = { 0.0f };
float carColors[MAX_CARS][3];

// Additional traffic obstacles: other motorcycles and food carts.
const int MAX_TRAFFIC_MOTORCYCLES = 15;
const int MAX_FOOD_CARTS = 10;
int trafficMotorcycleCount = 0;
int foodCartCount = 0;
float trafficMotorcyclePositions[MAX_TRAFFIC_MOTORCYCLES][2] = {};
float trafficMotorcycleAngles[MAX_TRAFFIC_MOTORCYCLES] = {};
float trafficMotorcycleColors[MAX_TRAFFIC_MOTORCYCLES][3] = {};
bool trafficMotorcycleIsParked[MAX_TRAFFIC_MOTORCYCLES] = {};
float foodCartPositions[MAX_FOOD_CARTS][2] = {};
float foodCartAngles[MAX_FOOD_CARTS] = {};
float foodCartColors[MAX_FOOD_CARTS][3] = {};
int foodCartParentBike[MAX_FOOD_CARTS] = {};
int foodCartSide[MAX_FOOD_CARTS] = {};
float motorcycleX = -3.0f;
float motorcycleY = 0.0f;
float motorcycleAngle = 0.0f;

// RestorePoint 1.3.5: acceleration, coasting friction, and speed limits.
// motorcycleVelocity is signed: positive = forward, negative = reverse.
float motorcycleVelocity = 0.0f;
const float maxForwardSpeed = 1.15f;   // world units per second
const float maxReverseSpeed = 0.45f;   // reverse is slower than forward
const float acceleration = 1.10f;      // speed gained per second while accelerating
const float friction = 1.80f;          // coasting speed lost per second
float turnSpeed = 100.0f;              // degrees per second at normal speed
bool moveForward = false;
bool moveBackward = false;
bool turnLeft = false;
bool turnRight = false;
bool collisionDetected = false;

// This is for the Collision

bool hitsRectangle(float x, float y,
    float halfW, float halfH,
    float left, float right,
    float bottom, float top)
{
    return (x + halfW > left &&
        x - halfW < right &&
        y + halfH > bottom &&
        y - halfH < top);
}

// Precise 2D oriented-rectangle overlap test (Separating Axis Theorem).
// Collision rectangles are slightly inset from visible outlines to avoid
// reporting a hit while there is still a noticeable visual gap.

bool orientedRectanglesOverlap(
    float ax, float ay, float aHalfW, float aHalfH, float aAngle,
    float bx, float by, float bHalfW, float bHalfH, float bAngle)
{
    const float radiansA = aAngle * 3.14159265f / 180.0f;
    const float radiansB = bAngle * 3.14159265f / 180.0f;
    const float ca = cos(radiansA), sa = sin(radiansA);
    const float cb = cos(radiansB), sb = sin(radiansB);
    const float axes[4][2] = {
        { ca, sa },{ -sa, ca },
        { cb, sb },{ -sb, cb }
    };
    const float dx = bx - ax;
    const float dy = by - ay;
    for (int i = 0; i < 4; ++i)
    {
        const float ux = axes[i][0];
        const float uy = axes[i][1];
        const float distance = fabs(dx * ux + dy * uy);
        const float radiusA =
            aHalfW * fabs(ca * ux + sa * uy) +
            aHalfH * fabs(-sa * ux + ca * uy);
        const float radiusB =
            bHalfW * fabs(cb * ux + sb * uy) +
            bHalfH * fabs(-sb * ux + cb * uy);
        // Touching is allowed; actual overlap triggers collision.
        if (distance >= radiusA + radiusB)
            return false;
    }
    return true;
}

bool checkCollision(float x, float y, float angle)
{
    float windowWidth = (float)glutGet(GLUT_WINDOW_WIDTH);
    float windowHeight = (float)glutGet(GLUT_WINDOW_HEIGHT);
    if (windowHeight <= 0.0f) windowHeight = 1.0f;
    const float aspect = windowWidth / windowHeight;
    // Visible motorcycle half-extents are about 0.24 x 0.40.
    // Use a modestly inset collision shape to reduce premature hits.
    const float bikeHalfW = 0.22f;
    const float bikeHalfH = 0.37f;
    const float radians = angle * 3.14159265f / 180.0f;
    const float c = fabs(cos(radians));
    const float s = fabs(sin(radians));
    const float rotatedW = (0.24f * c + 0.40f * s) / aspect;
    const float rotatedH = 0.24f * s + 0.40f * c;
    if (x - rotatedW < -4.0f || x + rotatedW > 4.0f ||
        y - rotatedH < -4.0f || y + rotatedH > 4.0f)
        return true;
    // Preserve the existing pavement collision regions.
    if (hitsRectangle(x, y, rotatedW, rotatedH,
        -5.0f, 5.0f, 3.0f, 5.0f))
        return true;
    if (hitsRectangle(x, y, rotatedW, rotatedH,
        -5.0f, -3.5f, -5.0f, -3.0f))
        return true;
    if (hitsRectangle(x, y, rotatedW, rotatedH,
        -2.3f, 1.5f, -5.0f, -3.0f))
        return true;
    if (hitsRectangle(x, y, rotatedW, rotatedH,
        2.8f, 5.0f, -5.0f, -3.0f))
        return true;
    // Compare vehicle bodies in screen-equivalent coordinates. This lets
    // the motorcycle's aspect-ratio correction and the unscaled car drawing
    // be represented consistently. SAT avoids oversized rotated AABB hits.
    for (int i = 0; i < carCount; ++i)
    {
        const float carX = carPositions[i][0];
        const float carY = carPositions[i][1];
        const float carHalfW = 0.205f * aspect; // visible tire edge ~= 0.22
        const float carHalfH = 0.33f;           // visible length ~= 0.35
        if (orientedRectanglesOverlap(
            x * aspect, y, bikeHalfW, bikeHalfH, angle,
            carX * aspect, carY, carHalfW, carHalfH, carAngles[i]))
            return true;
    }
    // Collision with other motorcycles. The box is slightly inset from
    // the visible model to avoid triggering before visual contact.
    for (int i = 0; i < trafficMotorcycleCount; ++i)
    {
        if (orientedRectanglesOverlap(
            x * aspect, y, bikeHalfW, bikeHalfH, angle,
            trafficMotorcyclePositions[i][0] * aspect,
            trafficMotorcyclePositions[i][1],
            0.205f, 0.36f, trafficMotorcycleAngles[i]))
            return true;
    }
    // Collision with food carts. Their box matches the outer cart body.
    for (int i = 0; i < foodCartCount; ++i)
    {
        if (orientedRectanglesOverlap(
            x * aspect, y, bikeHalfW, bikeHalfH, angle,
            foodCartPositions[i][0] * aspect,
            foodCartPositions[i][1],
            0.24f, 0.25f, foodCartAngles[i]))
            return true;
    }
    return false;
}

void handleCollisionDamage()
{
    int currentTime = glutGet(GLUT_ELAPSED_TIME);
    if (gameOver)
        return;
    if (currentTime - lastCollisionTime >=
        COLLISION_COOLDOWN_MS)
    {
        healthPoints--;
        lastCollisionTime = currentTime;
        cout << "Collision! HP remaining: "
            << healthPoints << endl;
        if (healthPoints <= 0)
        {
            healthPoints = 0;
            gameOver = true;
        }
    }
}

// Tells when Collision Is detected

void drawText(float x, float y, const char* text)
{
    glRasterPos3f(x, y, 0.90f);
    while (*text)
    {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *text);
        text++;
    }
}

// GLUT special-key callback records which controls are held. Motion itself
// is advanced by updateMovement(), producing frame-rate-independent movement.

void specialKeys(int key, int x, int y)
{
    if (gameOver) return;
    switch (key)
    {
    case GLUT_KEY_UP:    moveForward = true; break;
    case GLUT_KEY_DOWN:  moveBackward = true; break;
    case GLUT_KEY_LEFT:  turnLeft = true; break;
    case GLUT_KEY_RIGHT: turnRight = true; break;
    }
}

void specialKeyUp(int key, int x, int y)
{
    switch (key)
    {
    case GLUT_KEY_UP:    moveForward = false; break;
    case GLUT_KEY_DOWN:  moveBackward = false; break;
    case GLUT_KEY_LEFT:  turnLeft = false; break;
    case GLUT_KEY_RIGHT: turnRight = false; break;
    }
}

void updateMovement(int value)
{
    static int previousTime = 0;
    const int currentTime = glutGet(GLUT_ELAPSED_TIME);
    if (previousTime == 0) previousTime = currentTime;
    float dt = (currentTime - previousTime) / 1000.0f;
    previousTime = currentTime;
    // Clamp elapsed time so a lag spike cannot teleport the motorcycle.
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    if (!gameOver && dt > 0.0f)
    {
        // 1. Apply engine acceleration while a direction key is held.
        // If both or neither are held, throttle is zero and friction applies.
        const int throttle = (moveForward ? 1 : 0) -
            (moveBackward ? 1 : 0);
        if (throttle != 0)
        {
            motorcycleVelocity += throttle * acceleration * dt;
            if (motorcycleVelocity > maxForwardSpeed)
                motorcycleVelocity = maxForwardSpeed;
            if (motorcycleVelocity < -maxReverseSpeed)
                motorcycleVelocity = -maxReverseSpeed;
        }
        else
        {
            // 2. Friction gradually reduces speed when no direction is held.
            if (motorcycleVelocity > 0.0f)
            {
                motorcycleVelocity -= friction * dt;
                if (motorcycleVelocity < 0.0f)
                    motorcycleVelocity = 0.0f;
            }
            else if (motorcycleVelocity < 0.0f)
            {
                motorcycleVelocity += friction * dt;
                if (motorcycleVelocity > 0.0f)
                    motorcycleVelocity = 0.0f;
            }
        }
        float newAngle = motorcycleAngle;
        if (turnLeft != turnRight)
        {
            // Steering is slower at low speed, but can still turn in place.
            float speedRatio = fabs(motorcycleVelocity) / maxForwardSpeed;
            if (speedRatio > 1.0f) speedRatio = 1.0f;
            const float steeringFactor = 0.35f + 0.65f * speedRatio;
            newAngle += (turnLeft ? 1.0f : -1.0f) *
                turnSpeed * steeringFactor * dt;
        }
        // 3. Convert current velocity into distance traveled this update.
        float newX = motorcycleX;
        float newY = motorcycleY;
        const float travel = motorcycleVelocity * dt;
        const float radians = newAngle * 3.14159265f / 180.0f;
        newX -= sin(radians) * travel;
        newY += cos(radians) * travel;
        // 4. Keep collision handling in front of position updates.
        if (!checkCollision(newX, newY, newAngle))
        {
            // Movement is allowed
            motorcycleX = newX;
            motorcycleY = newY;
            motorcycleAngle = newAngle;
            collisionDetected = false;
            // Automatically complete the level once the motorcycle is fully
            // inside the green parking zone and correctly aligned.
            if (isParkingSuccessful())
            {
                completeParkingLevel();
            }
        }
        else
        {
            // Movement is blocked by an obstacle
            collisionDetected = true;
            // Handle HP deduction and Game Over
            handleCollisionDamage();
        }
        glutPostRedisplay();
    }
    glutTimerFunc(16, updateMovement, 0);
}

void drawParkingZone()
{
    // Green parking area
    glColor3f(0.08f, 0.45f, 0.12f);
    glBegin(GL_QUADS);
    glVertex2f(parkingX - PARK_HALF_W,
        parkingY - PARK_HALF_H);
    glVertex2f(parkingX + PARK_HALF_W,
        parkingY - PARK_HALF_H);
    glVertex2f(parkingX + PARK_HALF_W,
        parkingY + PARK_HALF_H);
    glVertex2f(parkingX - PARK_HALF_W,
        parkingY + PARK_HALF_H);
    glEnd();
    // Bright green border
    glColor3f(0.30f, 1.0f, 0.30f);
    glLineWidth(3.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(parkingX - PARK_HALF_W,
        parkingY - PARK_HALF_H);
    glVertex2f(parkingX + PARK_HALF_W,
        parkingY - PARK_HALF_H);
    glVertex2f(parkingX + PARK_HALF_W,
        parkingY + PARK_HALF_H);
    glVertex2f(parkingX - PARK_HALF_W,
        parkingY + PARK_HALF_H);
    glEnd();
}

bool isParkingSuccessful()
{
    // Match the visible motorcycle bounds, including the aspect correction.
    float windowWidth = (float)glutGet(GLUT_WINDOW_WIDTH);
    float windowHeight = (float)glutGet(GLUT_WINDOW_HEIGHT);
    if (windowHeight <= 0.0f) windowHeight = 1.0f;
    const float aspect = windowWidth / windowHeight;
    const float localHalfW = 0.24f;
    const float localHalfH = 0.40f;
    const float radians = motorcycleAngle * 3.14159265f / 180.0f;
    const float c = fabs(cos(radians));
    const float s = fabs(sin(radians));
    const float bikeHalfW = (localHalfW * c + localHalfH * s) / aspect;
    const float bikeHalfH = localHalfW * s + localHalfH * c;
    // Check whether the full visible motorcycle fits inside the zone.
    bool insideZone =
        motorcycleX - bikeHalfW >= parkingX - PARK_HALF_W &&
        motorcycleX + bikeHalfW <= parkingX + PARK_HALF_W &&
        motorcycleY - bikeHalfH >= parkingY - PARK_HALF_H &&
        motorcycleY + bikeHalfH <= parkingY + PARK_HALF_H;
    // Accept near-vertical orientations:
    // 0, 180, 360 degrees, with a 15-degree tolerance.
    float angle = fmod(fabs(motorcycleAngle), 360.0f);
    if (angle > 180.0f)
        angle = 360.0f - angle;
    bool aligned =
        angle <= 15.0f ||
        fabs(angle - 180.0f) <= 15.0f;
    return insideZone && aligned;
}

void completeParkingLevel()
{
    score += 100;
    level++;
    motorcycleX = -3.0f;
    motorcycleY = 0.0f;
    motorcycleAngle = 0.0f;
    motorcycleVelocity = 0.0f;
    collisionDetected = false;
    parkingAttemptFailed = false;
    generateLevel();
}

void generateLevel()
{
    // Build a new compact parking puzzle and randomized street obstacles.
    generatePuzzleCars();
    // The player starts at a random safe edge-of-road location each level.
    // This is called after all cars, parked motorcycles, and carts are placed.
    placePlayerAtRandomSpawn();
}

void addParkingVehicle(int col, int row)
{
    const float x = parkingSlotX(col) + ((rand() % 5) - 2) * 0.008f;
    const float y = parkingRowY(row) + ((rand() % 5) - 2) * 0.008f;
    // Some parking bays contain a motorcycle instead of a car.
    // Limit the number of parked NPC motorcycles so road traffic still exists.
    const bool useMotorcycle =
        (trafficMotorcycleCount < 2) && (rand() % 4 == 0);
    if (useMotorcycle)
    {
        const int i = trafficMotorcycleCount++;
        trafficMotorcyclePositions[i][0] = x;
        trafficMotorcyclePositions[i][1] = y;
        const float parkedAngles[5] = { -5.0f, -2.0f, 0.0f, 3.0f, 5.0f };
        trafficMotorcycleAngles[i] = parkedAngles[rand() % 5];
        trafficMotorcycleIsParked[i] = true;
        for (int channel = 0; channel < 3; ++channel)
            trafficMotorcycleColors[i][channel] = 0.15f + (rand() % 75) / 100.0f;
    }
    else if (carCount < MAX_CARS)
    {
        const int i = carCount++;
        carPositions[i][0] = x;
        carPositions[i][1] = y;
        carAngles[i] = 0.0f;
        for (int channel = 0; channel < 3; ++channel)
            carColors[i][channel] = 0.20f + (rand() % 71) / 100.0f;
    }
}

void generatePuzzleCars()
{
    updateParkingRowSpacing();
    carCount = 0;
    trafficMotorcycleCount = 0;
    foodCartCount = 0;
    // Pick a random target bay, avoiding the player's usual left-side start.
    const int TOTAL_SLOTS = PARKING_COLS * PARKING_ROWS;
    int candidates[TOTAL_SLOTS];
    int candidateCount = 0;
    for (int row = 0; row < PARKING_ROWS; ++row)
    {
        for (int col = 0; col < PARKING_COLS; ++col)
        {
            const float sx = parkingSlotX(col);
            const float sy = parkingRowY(row);
            if (fabs(sx + 3.0f) < 0.8f && fabs(sy) < 0.8f)
                continue;
            candidates[candidateCount++] = row * PARKING_COLS + col;
        }
    }
    const int targetSlot = candidates[rand() % candidateCount];
    const int targetRow = targetSlot / PARKING_COLS;
    const int targetCol = targetSlot % PARKING_COLS;
    parkingX = parkingSlotX(targetCol);
    parkingY = parkingRowY(targetRow);
    // Difficulty increases as the rows fill. Target column remains empty.
    int requestedVehicles = min(16, 6 + 2 * (level - 1));
    requestedVehicles = min(requestedVehicles,
        PARKING_ROWS * (PARKING_COLS - 1));
    int vehiclesPerRow[PARKING_ROWS] = { 0, 0, 0 };
    for (int i = 0; i < requestedVehicles; ++i)
    {
        const int startRow = rand() % PARKING_ROWS;
        for (int offset = 0; offset < PARKING_ROWS; ++offset)
        {
            const int row = (startRow + offset) % PARKING_ROWS;
            if (vehiclesPerRow[row] < PARKING_COLS - 1)
            {
                ++vehiclesPerRow[row];
                break;
            }
        }
    }
    // Pack cars and motorcycles into compact clusters on either side of target.
    for (int row = 0; row < PARKING_ROWS; ++row)
    {
        const int rowCount = vehiclesPerRow[row];
        const int leftCapacity = targetCol;
        const int rightCapacity = PARKING_COLS - targetCol - 1;
        const int minLeft = max(0, rowCount - rightCapacity);
        const int maxLeft = min(rowCount, leftCapacity);
        const int leftCount = minLeft + rand() % (maxLeft - minLeft + 1);
        const int rightCount = rowCount - leftCount;
        for (int j = 0; j < leftCount; ++j)
            addParkingVehicle(targetCol - leftCount + j, row);
        for (int j = 0; j < rightCount; ++j)
            addParkingVehicle(targetCol + 1 + j, row);
    }
    generateTrafficObstacles();
}

// Validates space for a side-road motorcycle or its attached trailer.

bool freeForTrafficObstacle(float x, float y, float halfW, float halfH)
{
    if (x - halfW < -3.85f || x + halfW > 3.85f ||
        y - halfH < -2.75f || y + halfH > 2.75f)
        return false;
    if (fabs(x - parkingX) < PARK_HALF_W + halfW + 0.10f &&
        fabs(y - parkingY) < PARK_HALF_H + halfH + 0.10f)
        return false;
    for (int i = 0; i < carCount; ++i)
    {
        if (fabs(x - carPositions[i][0]) < halfW + 0.23f + 0.07f &&
            fabs(y - carPositions[i][1]) < halfH + 0.35f + 0.08f)
            return false;
    }
    for (int i = 0; i < trafficMotorcycleCount; ++i)
    {
        if (fabs(x - trafficMotorcyclePositions[i][0]) < halfW + 0.22f + 0.10f &&
            fabs(y - trafficMotorcyclePositions[i][1]) < halfH + 0.38f + 0.10f)
            return false;
    }
    for (int i = 0; i < foodCartCount; ++i)
    {
        if (fabs(x - foodCartPositions[i][0]) < halfW + 0.26f + 0.10f &&
            fabs(y - foodCartPositions[i][1]) < halfH + 0.27f + 0.10f)
            return false;
    }
    return true;
}

void setBikeColor(int index)
{
    for (int channel = 0; channel < 3; ++channel)
        trafficMotorcycleColors[index][channel] = 0.15f + (rand() % 75) / 100.0f;
}

void createAttachedFoodCart(int bikeIndex, int sideSign)
{
    if (foodCartCount >= MAX_FOOD_CARTS)
        return;
    const int cart = foodCartCount++;
    const float radians = trafficMotorcycleAngles[bikeIndex] * 3.14159265f / 180.0f;
    float windowWidth = (float)glutGet(GLUT_WINDOW_WIDTH);
    float windowHeight = (float)glutGet(GLUT_WINDOW_HEIGHT);
    if (windowHeight <= 0.0f) windowHeight = 1.0f;
    const float aspect = windowWidth / windowHeight;
    // Attach the cart beside the motorcycle, not behind it. A positive sideSign
    // attaches on the bike's local right; a negative value attaches on its left.
    const float sideDistance = 0.54f;
    foodCartSide[cart] = (sideSign >= 0) ? 1 : -1;
    foodCartPositions[cart][0] = trafficMotorcyclePositions[bikeIndex][0]
        + foodCartSide[cart] * cos(radians) * sideDistance / aspect;
    foodCartPositions[cart][1] = trafficMotorcyclePositions[bikeIndex][1]
        + foodCartSide[cart] * sin(radians) * sideDistance;
    foodCartAngles[cart] = trafficMotorcycleAngles[bikeIndex];
    foodCartParentBike[cart] = bikeIndex;
    foodCartColors[cart][0] = 0.20f + (rand() % 75) / 100.0f;
    foodCartColors[cart][1] = 0.20f + (rand() % 75) / 100.0f;
    foodCartColors[cart][2] = 0.20f + (rand() % 75) / 100.0f;
}

void generateTrafficObstacles()
{
    // Preserve motorcycles already chosen as alternatives to cars in parking bays.
    const int parkedBikeCount = trafficMotorcycleCount;
    for (int i = 0; i < parkedBikeCount; ++i)
        trafficMotorcycleIsParked[i] = true;
    foodCartCount = 0;
    const int desiredTotalBikes = min(MAX_TRAFFIC_MOTORCYCLES,
        max(3, 2 + (level - 1) / 2));
    int attempts = 0;
    // Place additional full-size motorcycles in side aisles. A subset will tow carts.
    while (trafficMotorcycleCount < desiredTotalBikes && attempts < 900)
    {
        ++attempts;
        const float sideSign = (rand() % 2 == 0) ? -1.0f : 1.0f;
        const float candidateX = sideSign * (2.35f + (rand() % 136) / 100.0f);
        const float candidateY = -2.35f + (rand() % 471) / 100.0f;
        const float angleOptions[4] = { 0.0f, 180.0f, -8.0f, 8.0f };
        const float angle = angleOptions[rand() % 4];
        if (!freeForTrafficObstacle(candidateX, candidateY, 0.23f, 0.38f))
            continue;
        // Roughly half tow a cart. Ensure at least one cart appears when possible.
        const bool tryCart = foodCartCount < MAX_FOOD_CARTS &&
            (foodCartCount == 0 || (rand() % 2 == 0));
        const int cartSide = (rand() % 2 == 0) ? -1 : 1;
        if (tryCart)
        {
            const float radians = angle * 3.14159265f / 180.0f;
            float windowWidth = (float)glutGet(GLUT_WINDOW_WIDTH);
            float windowHeight = (float)glutGet(GLUT_WINDOW_HEIGHT);
            if (windowHeight <= 0.0f) windowHeight = 1.0f;
            const float aspect = windowWidth / windowHeight;
            const float cartX = candidateX + cartSide * cos(radians) * 0.54f / aspect;
            const float cartY = candidateY + cartSide * sin(radians) * 0.54f;
            if (!freeForTrafficObstacle(cartX, cartY, 0.25f, 0.27f))
                continue;
        }
        const int bike = trafficMotorcycleCount++;
        trafficMotorcyclePositions[bike][0] = candidateX;
        trafficMotorcyclePositions[bike][1] = candidateY;
        trafficMotorcycleAngles[bike] = angle;
        trafficMotorcycleIsParked[bike] = false;
        setBikeColor(bike);
        if (tryCart)
            createAttachedFoodCart(bike, cartSide);
    }
    // On higher levels, add another cart to a bike that does not already tow one,
    // while preserving the cart's attached position and matching orientation.
    if (foodCartCount == 0 && trafficMotorcycleCount > 0)
    {
        for (int i = 0; i < trafficMotorcycleCount; ++i)
        {
            if (!trafficMotorcycleIsParked[i])
            {
                createAttachedFoodCart(i, 1);
                break;
            }
        }
    }
}

void placePlayerAtRandomSpawn()
{
    motorcycleVelocity = 0.0f;
    // Start the player randomly in a safe edge lane. This prevents spawning
    // on top of puzzle cars, other motorcycles, food carts, or the target.
    for (int attempt = 0; attempt < 500; ++attempt)
    {
        float sideSign = (rand() % 2 == 0) ? -1.0f : 1.0f;
        float candidateX = sideSign * (2.75f + (rand() % 81) / 100.0f);
        float candidateY = -2.35f + (rand() % 471) / 100.0f;
        float candidateAngle = (rand() % 5 - 2) * 3.0f;
        // Keep the new spawn away from the green target to prevent a new
        // level from completing immediately when the player appears.
        if (fabs(candidateX - parkingX) < PARK_HALF_W + 0.30f &&
            fabs(candidateY - parkingY) < PARK_HALF_H + 0.45f)
        {
            continue;
        }
        if (!checkCollision(candidateX, candidateY, candidateAngle))
        {
            motorcycleX = candidateX;
            motorcycleY = candidateY;
            motorcycleAngle = candidateAngle;
            return;
        }
    }
    // Safe fallback used only if random placement attempts all fail.
    motorcycleX = -3.0f;
    motorcycleY = 0.0f;
    motorcycleAngle = 0.0f;
}

void drawTrafficMotorcycle(int index)
{
    float windowWidth = (float)glutGet(GLUT_WINDOW_WIDTH);
    float windowHeight = (float)glutGet(GLUT_WINDOW_HEIGHT);
    if (windowHeight <= 0.0f) windowHeight = 1.0f;
    float aspect = windowWidth / windowHeight;
    glPushMatrix();
    glTranslatef(trafficMotorcyclePositions[index][0],
        trafficMotorcyclePositions[index][1], 0.20f);
    glScalef(1.0f / aspect, 1.0f, 1.0f);
    glRotatef(trafficMotorcycleAngles[index], 0.0f, 0.0f, 1.0f);
    glScalef(4.0f, 4.0f, 1.0f);
    // Tires
    glColor3f(0.04f, 0.04f, 0.04f);
    glBegin(GL_QUADS);
    glVertex2f(-0.014f, -0.095f);
    glVertex2f(0.014f, -0.095f);
    glVertex2f(0.014f, -0.055f);
    glVertex2f(-0.014f, -0.055f);
    glVertex2f(-0.014f, 0.055f);
    glVertex2f(0.014f, 0.055f);
    glVertex2f(0.014f, 0.095f);
    glVertex2f(-0.014f, 0.095f);
    glEnd();
    // Body
    glColor3f(trafficMotorcycleColors[index][0],
        trafficMotorcycleColors[index][1],
        trafficMotorcycleColors[index][2]);
    glBegin(GL_QUADS);
    glVertex2f(-0.022f, -0.055f);
    glVertex2f(0.022f, -0.055f);
    glVertex2f(0.025f, 0.050f);
    glVertex2f(-0.025f, 0.050f);
    glEnd();
    // Seat and handlebar
    glColor3f(0.08f, 0.08f, 0.08f);
    glBegin(GL_QUADS);
    glVertex2f(-0.016f, -0.035f);
    glVertex2f(0.016f, -0.035f);
    glVertex2f(0.016f, 0.015f);
    glVertex2f(-0.016f, 0.015f);
    glEnd();
    glColor3f(0.75f, 0.75f, 0.75f);
    glBegin(GL_QUADS);
    glVertex2f(-0.055f, 0.045f);
    glVertex2f(0.055f, 0.045f);
    glVertex2f(0.055f, 0.052f);
    glVertex2f(-0.055f, 0.052f);
    glEnd();
    glPopMatrix();
}

void drawFoodCart(int index)
{
    float windowWidth = (float)glutGet(GLUT_WINDOW_WIDTH);
    float windowHeight = (float)glutGet(GLUT_WINDOW_HEIGHT);
    if (windowHeight <= 0.0f) windowHeight = 1.0f;
    float aspect = windowWidth / windowHeight;
    glPushMatrix();
    glTranslatef(foodCartPositions[index][0],
        foodCartPositions[index][1], 0.10f);
    glScalef(1.0f / aspect, 1.0f, 1.0f);
    glRotatef(foodCartAngles[index], 0.0f, 0.0f, 1.0f);
    // Side hitch bar connects the cart's near edge to the motorcycle side.
    // Its direction mirrors according to which side of the bike the cart uses.
    glColor3f(0.12f, 0.12f, 0.12f);
    glBegin(GL_QUADS);
    if (foodCartSide[index] >= 0)
    {
        glVertex2f(-0.55f, -0.025f);
        glVertex2f(-0.22f, -0.025f);
        glVertex2f(-0.22f, 0.025f);
        glVertex2f(-0.55f, 0.025f);
    }
    else
    {
        glVertex2f(0.22f, -0.025f);
        glVertex2f(0.55f, -0.025f);
        glVertex2f(0.55f, 0.025f);
        glVertex2f(0.22f, 0.025f);
    }
    glEnd();
    // Small vendor cart body
    glColor3f(0.45f, 0.24f, 0.10f);
    glBegin(GL_QUADS);
    glVertex2f(-0.23f, -0.25f);
    glVertex2f(0.23f, -0.25f);
    glVertex2f(0.23f, 0.19f);
    glVertex2f(-0.23f, 0.19f);
    glEnd();
    // Colorful canopy/awning
    glColor3f(foodCartColors[index][0],
        foodCartColors[index][1],
        foodCartColors[index][2]);
    glBegin(GL_QUADS);
    glVertex2f(-0.26f, 0.05f);
    glVertex2f(0.26f, 0.05f);
    glVertex2f(0.26f, 0.27f);
    glVertex2f(-0.26f, 0.27f);
    glEnd();
    // Awning stripes
    glColor3f(1.0f, 0.92f, 0.70f);
    glBegin(GL_QUADS);
    glVertex2f(-0.13f, 0.05f);
    glVertex2f(-0.045f, 0.05f);
    glVertex2f(-0.045f, 0.27f);
    glVertex2f(-0.13f, 0.27f);
    glVertex2f(0.045f, 0.05f);
    glVertex2f(0.13f, 0.05f);
    glVertex2f(0.13f, 0.27f);
    glVertex2f(0.045f, 0.27f);
    glEnd();
    // Wheels, kept within the cart's collision footprint
    glColor3f(0.04f, 0.04f, 0.04f);
    glBegin(GL_QUADS);
    glVertex2f(-0.25f, -0.20f);
    glVertex2f(-0.19f, -0.20f);
    glVertex2f(-0.19f, -0.08f);
    glVertex2f(-0.25f, -0.08f);
    glVertex2f(0.19f, -0.20f);
    glVertex2f(0.25f, -0.20f);
    glVertex2f(0.25f, -0.08f);
    glVertex2f(0.19f, -0.08f);
    glEnd();
    glPopMatrix();
}

// Right-click toggles the HUD.
void mouse(int button, int state, int x, int y)
{
    if (button == GLUT_RIGHT_BUTTON &&
        state == GLUT_DOWN)
    {
        showHUD = !showHUD;
        glutPostRedisplay();
    }
}

void keyboard(unsigned char key, int x, int y)
{
    // Escape exits cleanly. GLUT's main loop otherwise runs continuously.
    if (key == 27)
        exit(EXIT_SUCCESS);

    // H toggles the entire HUD, even when the game is over.
    if (key == 'h' || key == 'H')
    {
        showHUD = !showHUD;
        glutPostRedisplay();
        return;
    }

    // Restart the whole game after Game Over.
    if (gameOver)
    {
        if (key == 'r' || key == 'R')
        {
            score = 0;
            level = 1;
            healthPoints = MAX_HEALTH;
            motorcycleX = -3.0f;
            motorcycleY = 0.0f;
            motorcycleAngle = 0.0f;
            motorcycleVelocity = 0.0f;
            collisionDetected = false;
            parkingAttemptFailed = false;
            gameOver = false;
            moveForward = moveBackward = turnLeft = turnRight = false;
            generateLevel();
        }
        glutPostRedisplay();
        return;
    }
    // Existing motorcycle reset
    if (key == 'r' || key == 'R')
    {
        placePlayerAtRandomSpawn();
        collisionDetected = false;
        parkingAttemptFailed = false;
    }
    else if (key == ' ')
    {
        // Parking completes automatically when movement puts the bike
        // fully inside the target and aligned. Space only checks feedback.
        if (!isParkingSuccessful())
            parkingAttemptFailed = true;
    }
    glutPostRedisplay();
}

// Motorcyle Model

void drawMotorcycle()
{
    glPushMatrix();
    // Get actual window dimensions
    float windowWidth = (float)glutGet(GLUT_WINDOW_WIDTH);
    float windowHeight = (float)glutGet(GLUT_WINDOW_HEIGHT);
    if (windowHeight <= 0.0f)
        windowHeight = 1.0f;
    float aspect = windowWidth / windowHeight;
    glTranslatef(motorcycleX, motorcycleY, 0.30f);
    // Fix screen stretching BEFORE rotation
    glScalef(1.0f / aspect, 1.0f, 1.0f);
    // Rotate motorcycle
    glRotatef(motorcycleAngle, 0.0f, 0.0f, 1.0f);
    // Make motorcycle larger without distortion
    glScalef(4.0f, 4.0f, 1.0f);
    // Rear tire
    glColor3f(0.05f, 0.05f, 0.05f);
    glBegin(GL_QUADS);
    glVertex2f(-0.015f, -0.100f);
    glVertex2f(0.015f, -0.100f);
    glVertex2f(0.015f, -0.060f);
    glVertex2f(-0.015f, -0.060f);
    glEnd();
    // Front tire
    glBegin(GL_QUADS);
    glVertex2f(-0.015f, 0.060f);
    glVertex2f(0.015f, 0.060f);
    glVertex2f(0.015f, 0.100f);
    glVertex2f(-0.015f, 0.100f);
    glEnd();
    // Handlebars
    glColor3f(0.70f, 0.70f, 0.70f);
    glBegin(GL_QUADS);
    glVertex2f(-0.060f, 0.045f);
    glVertex2f(0.060f, 0.045f);
    glVertex2f(0.060f, 0.055f);
    glVertex2f(-0.060f, 0.055f);
    glEnd();
    // Motorcycle red body (vertex array)
    GLfloat body[] = {
        -0.018f, -0.070f,
         0.018f, -0.070f,
         0.025f, -0.040f,
         0.025f,  0.040f,
         0.018f,  0.070f,
        -0.018f,  0.070f,
        -0.025f,  0.040f,
        -0.025f, -0.040f
    };
    glColor3f(0.90f, 0.10f, 0.10f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, body);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 8);
    glDisableClientState(GL_VERTEX_ARRAY);
    // Black seat
    glColor3f(0.10f, 0.10f, 0.10f);
    glBegin(GL_QUADS);
    glVertex2f(-0.018f, -0.045f);
    glVertex2f(0.018f, -0.045f);
    glVertex2f(0.018f, 0.015f);
    glVertex2f(-0.018f, 0.015f);
    glEnd();
    // Headlight
    glColor3f(1.0f, 0.95f, 0.40f);
    glBegin(GL_QUADS);
    glVertex2f(-0.012f, 0.060f);
    glVertex2f(0.012f, 0.060f);
    glVertex2f(0.012f, 0.075f);
    glVertex2f(-0.012f, 0.075f);
    glEnd();
    glPopMatrix();
}

void drawaCar(float carX, float carY, int carIndex)
{
    glPushMatrix();
    glTranslatef(carX, carY, 0.0f);
    // Rotate this car according to its stored direction.
    glRotatef(carAngles[carIndex], 0.0f, 0.0f, 1.0f);
    glColor3f(
        carColors[carIndex][0],
        carColors[carIndex][1],
        carColors[carIndex][2]
    );
    glBegin(GL_QUADS);
    glVertex2f(-0.18f, -0.35f);
    glVertex2f(0.18f, -0.35f);
    glVertex2f(0.18f, 0.35f);
    glVertex2f(-0.18f, 0.35f);
    glEnd();
    // Front windshield
    glColor3f(0.55f, 0.80f, 0.95f);
    glBegin(GL_QUADS);
    glVertex2f(-0.14f, 0.10f);
    glVertex2f(0.14f, 0.10f);
    glVertex2f(0.14f, 0.24f);
    glVertex2f(-0.14f, 0.24f);
    glEnd();
    // Rear windshield
    glColor3f(0.35f, 0.60f, 0.80f);
    glBegin(GL_QUADS);
    glVertex2f(-0.14f, -0.25f);
    glVertex2f(0.14f, -0.25f);
    glVertex2f(0.14f, -0.13f);
    glVertex2f(-0.14f, -0.13f);
    glEnd();
    // Left and right tires
    glColor3f(0.05f, 0.05f, 0.05f);
    glBegin(GL_QUADS);
    // Front left
    glVertex2f(-0.22f, 0.18f);
    glVertex2f(-0.18f, 0.18f);
    glVertex2f(-0.18f, 0.29f);
    glVertex2f(-0.22f, 0.29f);
    // Front right
    glVertex2f(0.18f, 0.18f);
    glVertex2f(0.22f, 0.18f);
    glVertex2f(0.22f, 0.29f);
    glVertex2f(0.18f, 0.29f);
    // Rear left
    glVertex2f(-0.22f, -0.29f);
    glVertex2f(-0.18f, -0.29f);
    glVertex2f(-0.18f, -0.18f);
    glVertex2f(-0.22f, -0.18f);
    // Rear right
    glVertex2f(0.18f, -0.29f);
    glVertex2f(0.22f, -0.29f);
    glVertex2f(0.22f, -0.18f);
    glVertex2f(0.18f, -0.18f);
    glEnd();
    // Headlights
    glColor3f(1.0f, 0.95f, 0.60f);
    glBegin(GL_QUADS);
    glVertex2f(-0.14f, 0.30f);
    glVertex2f(-0.07f, 0.30f);
    glVertex2f(-0.07f, 0.35f);
    glVertex2f(-0.14f, 0.35f);
    glVertex2f(0.07f, 0.30f);
    glVertex2f(0.14f, 0.30f);
    glVertex2f(0.14f, 0.35f);
    glVertex2f(0.07f, 0.35f);
    glEnd();
    // Brake lights
    glColor3f(0.95f, 0.10f, 0.10f);
    glBegin(GL_QUADS);
    glVertex2f(-0.14f, -0.35f);
    glVertex2f(-0.07f, -0.35f);
    glVertex2f(-0.07f, -0.31f);
    glVertex2f(-0.14f, -0.31f);
    glVertex2f(0.07f, -0.35f);
    glVertex2f(0.14f, -0.35f);
    glVertex2f(0.14f, -0.31f);
    glVertex2f(0.07f, -0.31f);
    glEnd();
    glPopMatrix();
}

// Add Procedural levels where These cars and motorcycles switch places and ur goal parking also changes
// Add Scoring and Health Collision.

void display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // BACKGROUND LAYER: pavement and road markings sit behind all vehicles.
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, -0.75f);

    //Bottom Pavement Middle
    glColor4f(0.31f, 0.31f, 0.34f, 1.0);
    glBegin(GL_QUADS);
    glVertex2f(-2.3f, -3.0f);
    glVertex2f(1.5f, -3.0f);
    glVertex2f(1.5f, -5.0f);
    glVertex2f(-2.3f, -5.0f);
    //Bottom Pavement Right
    glVertex2f(2.8f, -3.0f);
    glVertex2f(5.0f, -3.0f);
    glVertex2f(5.0f, -5.0f);
    glVertex2f(2.8f, -5.0f);
    // Top Pavement
    glVertex2f(-5.0f, 3.0f);
    glVertex2f(5.0f, 3.0f);
    glVertex2f(5.0f, 5.0f);
    glVertex2f(-5.0f, 5.0f);
    // Bottom Left Pavement
    glVertex2f(-5.0f, -3.0f);
    glVertex2f(-3.5f, -3.0f);
    glVertex2f(-3.5f, -5.0f);
    glVertex2f(-5.0f, -5.0f);


    glEnd();
    //Bottom outline For Pavement RIght
    glColor3f(1.0f, 0.73f, 0.03f);
    glLineWidth(5.0f);
    glBegin(GL_LINES);
    // Right Pavement Outline
    glVertex2f(2.8f, -3.0f);
    glVertex2f(5.0f, -3.0f);
    // Middle Pavement Outline
    glVertex2f(-2.3f, -3.0f);
    glVertex2f(1.5f, -3.0f);
    // Top Pavement Outline
    glVertex2f(-5.0f, 3.0f);
    glVertex2f(5.0f, 3.0f);
    // Bottom Left Pavement Outline
    glVertex2f(-5.0f, -3.0f);
    glVertex2f(-3.5f, -3.0f);
    glEnd();
    // Parking
    glColor3f(0.5f, 0.5f, 0.5f);
    glLineWidth(5.0f);
    glBegin(GL_LINES);
    // Right
    glVertex2f(1.6f, -3.0f);
    glVertex2f(2.7f, -3.0f);
    glVertex2f(1.6f, -3.3f);
    glVertex2f(2.7f, -3.3f);
    glVertex2f(1.6f, -3.6f);
    glVertex2f(2.7f, -3.6f);
    glVertex2f(1.6f, -3.9f);
    glVertex2f(2.7f, -3.9f);
    //Left
    glVertex2f(-3.4f, -3.0f);
    glVertex2f(-2.4f, -3.0f);
    glVertex2f(-3.4f, -3.3f);
    glVertex2f(-2.4f, -3.3f);
    glVertex2f(-3.4f, -3.6f);
    glVertex2f(-2.4f, -3.6f);
    glVertex2f(-3.4f, -3.9f);
    glVertex2f(-2.4f, -3.9f);
    glEnd();




    // The busy parking area deliberately has no painted bay lines.
    // Vehicle placements define the loose, informal parking layout.
    // Filled red circle
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(1.0f, 1.0f); // Center of circle
    for (int i = 0; i <= 360; i++)
    {
        float angle = i * 3.14159f / 180.0f;
        float x = 1.0f + 0.15f * cos(angle);
        float y = 1.0f + 0.25f * sin(angle);
        glVertex2f(x, y);
    }
    glEnd();
    // Finish the background/pavement layer.
    glPopMatrix();
    // TARGET LAYER: slightly above pavement, below vehicles.
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, -0.35f);
    drawParkingZone();
    glPopMatrix();
    // Draw all generated cars
    for (int i = 0; i < carCount; i++)
    {
        drawaCar(carPositions[i][0], carPositions[i][1], i);
    }
    // Draw attached carts first, then motorcycles above them so the hitch
    // joins neatly to the bike's side without covering its body.
    for (int i = 0; i < foodCartCount; ++i)
        drawFoodCart(i);
    for (int i = 0; i < trafficMotorcycleCount; ++i)
        drawTrafficMotorcycle(i);
    drawMotorcycle();
    // Draw the top-left HUD only when enabled.
    if (showHUD)
    {
        // Controls and the key that toggles this panel.
        glColor3f(1.0f, 1.0f, 1.0f);
        drawText(-3.8f, 3.5f, "H: Hide HUD | Arrows: Move/Turn | R: Reset");

        // Current collision state.
        if (collisionDetected)
        {
            glColor3f(1.0f, 0.2f, 0.2f);
            drawText(-3.8f, 3.2f, "COLLISION!");
        }
        else
        {
            glColor3f(0.2f, 1.0f, 0.2f);
            drawText(-3.8f, 3.2f, "SAFE");
        }

        // Score, level, and parking instructions.
        glColor3f(1.0f, 1.0f, 1.0f);
        string scoreText = "Score: " + to_string(score);
        string levelText = "Level: " + to_string(level);
        drawText(-3.8f, 2.9f, scoreText.c_str());
        drawText(-3.8f, 2.6f, levelText.c_str());
        drawText(-3.8f, 2.3f, "Park fully in the green zone to advance");

        if (parkingAttemptFailed)
        {
            glColor3f(1.0f, 0.25f, 0.25f);
            drawText(-3.8f, 2.0f, "Not parked correctly!");
        }

        // Health and active obstacle counts.
        glColor3f(1.0f, 0.25f, 0.25f);
        string hpText = "HP: " + to_string(healthPoints) +
            " / " + to_string(MAX_HEALTH);
        drawText(-3.8f, 1.7f, hpText.c_str());

        glColor3f(1.0f, 1.0f, 1.0f);
        string carsText = "Cars: " + to_string(carCount);
        string trafficText = "NPC motorcycles: " +
            to_string(trafficMotorcycleCount) +
            "  Attached food carts: " + to_string(foodCartCount);
        drawText(-3.8f, 1.4f, carsText.c_str());
        drawText(-3.8f, 1.1f, trafficText.c_str());
    }
    else
    {
        // Keep a small reminder visible so the player knows how to restore the HUD.
        glColor3f(1.0f, 1.0f, 1.0f);
        drawText(-3.8f, 3.5f, "HUD hidden - Press H to show");
    }

    // Game Over message
    if (gameOver)
    {
        glColor3f(1.0f, 0.15f, 0.15f);
        drawText(
            -1.5f, 0.5f,
            "GAME OVER"
        );
        glColor3f(1.0f, 1.0f, 1.0f);
        drawText(
            -2.2f, 0.1f,
            "Press R to Restart"
        );
    }
    glFlush();
} // End of display()

void init()
{
    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClearDepth(1.0);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-4, 4, -4, 4, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

int main(int argc, char** argv)
{
    // Seed random placement once. The interactive GLUT loop ends when the user exits.
    srand(static_cast<unsigned int>(time(nullptr)));
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_SINGLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(700, 600);
    glutCreateWindow("Motorcycle Parking Game - Simplified Build");
    glutFullScreen();
    init();
    // Create the first parking challenge

    generateLevel();
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKeys);
    glutSpecialUpFunc(specialKeyUp);
    glutMouseFunc(mouse);
    glutTimerFunc(16, updateMovement, 0);
    glutMainLoop();
    return 0;
}
