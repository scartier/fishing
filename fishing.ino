#include <commlib.h>

byte faceOffsetArray[] = { 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5 };

#define CCW_FROM_FACE(f, amt) faceOffsetArray[6 + (f) - (amt)]
#define CW_FROM_FACE(f, amt)  faceOffsetArray[(f) + (amt)]
#define OPPOSITE_FACE(f)      CW_FROM_FACE((f), 3)
#define RGB_TO_U16(r,g,b) ((((uint16_t)(r)>>3) & 0x1F)<<1 | (((uint16_t)(g)>>3) & 0x1F)<<6 | (((uint16_t)(b)>>3) & 0x1F)<<11)

#define DONT_CARE 0
#define INVALID_FACE 7

// ----------------------------------------------------------------------------------------------------

uint32_t randState = 123;

// ----------------------------------------------------------------------------------------------------

enum TileType : uint8_t
{
  TileType_NotPresent,    // no neighbor present
  TileType_Unknown,       // new neighbor, but we don't know what it is yet
  TileType_Lake,
  TileType_Player,
};

enum LakeDepth : uint8_t
{
  LakeDepth_Unknown,
  LakeDepth_Shallow,
  LakeDepth_Medium,
  LakeDepth_Deep,
};

// ----------------------------------------------------------------------------------------------------
// FISH

#define FISH_SPAWN_RATE_MIN (5000>>7)
#define FISH_SPAWN_RATE_MAX (10000>>7)
byte numFishToSpawn = 1;
Timer spawnTimer;
byte fishSpawnDelay = 0;
byte fishMoveTargetTiles[FACE_COUNT];

#define BROADCAST_DELAY_RATE 1000
Timer broadcastDelay;

enum FishType : uint8_t
{
  FishType_None,
  FishType_Small,
};

struct FishInfo
{
  FishType fishType;
  byte curFace;
  Timer moveTimer;
  byte destNeighbor;
  byte destFace;
};

#define FISH_MOVE_RATE_NORMAL 500
#define FISH_MOVE_RATE_PAUSE 3000

#define MAX_FISH_PER_TILE 3
FishInfo residentFish[MAX_FISH_PER_TILE];

// ----------------------------------------------------------------------------------------------------
// FISH MOVEMENT

byte nextMoveFaceArray[6][6] =
{
  { 0, 0, 1, 2, 5, 0 }, // Dest face 0 - 50/50 CCW
  { 1, 1, 1, 2, 5, 0 }, // Dest face 1 - 50/50 CW
  { 1, 2, 2, 2, 3, 4 }, // Dest face 2 - 50/50 CCW
  { 1, 2, 3, 3, 3, 4 }, // Dest face 3 - 50/50 CW
  { 5, 0, 3, 4, 4, 4 }, // Dest face 4 - 50/50 CCW
  { 5, 0, 3, 4, 5, 5 }, // Dest face 5 - 50/50 CW
};

// ----------------------------------------------------------------------------------------------------
// TILE

struct TileInfo
{
  TileType tileType;
  LakeDepth lakeDepth;
};

TileInfo tileInfo;
LakeDepth prevLakeDepth;
TileInfo neighborTileInfo[FACE_COUNT];
bool sendOurTileInfo = false;
byte numNeighborLakeTiles = 0;

// ----------------------------------------------------------------------------------------------------
// PLAYER

#define MAX_PLAYER_COLORS 7
uint16_t playerColors[] =
{
  RGB_TO_U16( 160,  32,   0 ),
  RGB_TO_U16( 192, 192,  96 ),
  RGB_TO_U16( 224, 160,   0 ),
  RGB_TO_U16( 112, 112,   0 ),
  RGB_TO_U16( 216, 144,   0 ),
  RGB_TO_U16(  16, 184,   0 ),
  RGB_TO_U16( 128,   0, 128 ),
};

byte playerColorIndex = 0;

enum PlayerState
{
  PlayerState_Idle,           // detached from lake
  PlayerState_GetCastAngle,   // sweeping left/right waiting for player to click
  PlayerState_GetCastPower,   // sweeping forward/back waiting for player to release
  PlayerState_Casting,
  PlayerState_WaitingForFish
};
PlayerState playerState = PlayerState_Idle;

#define CAST_ANGLE_SWEEP_INC 10
byte castFaceStart, castFaceEnd;
byte castFace, castAngle;
#define CAST_POWER_SWEEP_INC 5
byte castPower;

enum CastSweepDirection
{
  CastSweepDirection_CW,
  CastSweepDirection_CCW,

  CastSweepDirection_Up = CastSweepDirection_CW,
  CastSweepDirection_Down = CastSweepDirection_CCW,
};
CastSweepDirection castSweepDirection;    // true = forward, false = backward

// ----------------------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------------------

enum Command : uint8_t
{
  Command_TileType,
  Command_LakeDepth,
  Command_FishSpawned,      // Broadcast out when a tile spawns a fish - temporarily inhibits spawning in other tiles
  Command_FishTransfer,     // Sent when one tile is trying to transfer a fish to a neighbor
  Command_FishAccepted,     // Response indicating the transfer was accepted

  Command_CastDir0,
  Command_CastDir1,
  Command_CastDir2,
  Command_CastDir3,
};

// ====================================================================================================

void setup()
{
  // Temporary random seed is generated detiministically from our tile's serial number
  uint32_t serial_num_32 = 2463534242UL;    // We start with  Marsaglia's seed...
  // ... and then fold in bits from our serial number
  for( byte i=0; i< SERIAL_NUMBER_LEN; i++ )
  {
    serial_num_32 ^=  getSerialNumberByte(i) << (i * 3);
  }

  randState=serial_num_32;

  resetLakeTile();
}

// ----------------------------------------------------------------------------------------------------

void resetLakeTile()
{
  tileInfo.tileType = TileType_Lake;
  tileInfo.lakeDepth = LakeDepth_Unknown;

  numFishToSpawn = 1;
  resetSpawnTimer();

  for (byte fish = 0; fish < MAX_FISH_PER_TILE; fish++)
  {
    residentFish[fish].fishType = FishType_None;
  }
}

// ----------------------------------------------------------------------------------------------------

void resetPlayerTile()
{
  tileInfo.tileType = TileType_Player;
  playerState = PlayerState_Idle;
}

// ====================================================================================================
// COMMUNICATION
// ====================================================================================================

// ----------------------------------------------------------------------------------------------------

void processCommForFace(byte commandByte, byte value, byte f)
{
  Command command = (Command) commandByte;

  switch (command)
  {
    case Command_TileType:
      if (neighborTileInfo[f].tileType != (TileType) value)
      {
        neighborTileInfo[f].tileType = (TileType) value;
        if (neighborTileInfo[f].tileType == TileType_Lake)
        {
          // Expect the next comm to be the neighbor's depth
          neighborTileInfo[f].lakeDepth = LakeDepth_Unknown;
        }

        // Force the player to recompute casting info
        playerState = PlayerState_Idle;
      }
      break;

    case Command_LakeDepth:
      processNeighborLakeDepth(f, (LakeDepth) value);
      break;

    case Command_FishSpawned:
      if (tileInfo.tileType == TileType_Lake)
      {
        if (broadcastDelay.isExpired())
        {
          // ~1 sec cummulative delay for each subsequent fish spawn : 8*128 = 1024 ms
          if (fishSpawnDelay < 150) fishSpawnDelay += 8;

          broadcastCommToAllNeighbors(Command_FishSpawned, DONT_CARE);
          resetSpawnTimer();
          broadcastDelay.set(BROADCAST_DELAY_RATE);
        }
      }
      break;

    case Command_FishTransfer:
      // Neighbor tile is trying to send us their fish.

      // Check if we have room for this fish
      for (byte fishIndex = 0; fishIndex < MAX_FISH_PER_TILE; fishIndex++)
      {
        if (residentFish[fishIndex].fishType == FishType_None)
        {
          // Found an empty slot for a fish
          // Accept the fish and assign it a random destination
          enqueueCommOnFace(f, Command_FishAccepted, value);
          residentFish[fishIndex].fishType = FishType_Small;
          residentFish[fishIndex].curFace = f;
          assignFishMoveTarget(fishIndex);
          residentFish[fishIndex].moveTimer.set(FISH_MOVE_RATE_NORMAL);
          break;
        }
      }
      break;

    case Command_FishAccepted:
      // Neighbor tile accepted our request to give them our fish
      // Remove the fish from our list
      if (residentFish[value].fishType != FishType_None)
      {
        residentFish[value].fishType = FishType_None;
      }
      break;
  }
}

// ----------------------------------------------------------------------------------------------------

void processNeighborLakeDepth(byte f, LakeDepth newNeighborLakeDepth)
{
  // Doesn't make sense to do this for non-lake tiles
  if (tileInfo.tileType != TileType_Lake)
  {
    return;
  }

  //LakeDepth oldNeighborLakeDepth = neighborTileInfo[f].lakeDepth;
  neighborTileInfo[f].lakeDepth = newNeighborLakeDepth;

  // Check if our depth changed as a result of our neighbor changing
  determineOurLakeDepth();
}

void determineOurLakeDepth()
{
  // If our own depth is 'Shallow' then nothing will change that
  if (tileInfo.lakeDepth == LakeDepth_Shallow)
  {
    return;
  }

  // If we got here then we must be completely surrounded by lake tiles (ie. not 'Shallow')

  // Compute our depth based on our neighbors. We will be one step deeper than the shallowest neighbor.
  // In practice, this means that any 'Shallow' neighbor forces us to be 'Medium', otherwise we are 'Deep'
  // Start by assuming we are 'Deep'
  tileInfo.lakeDepth = LakeDepth_Deep;
  FOREACH_FACE(f)
  {
    if (neighborTileInfo[f].lakeDepth == LakeDepth_Shallow)
    {
      // Found a 'Shallow' neighbor - no sense continuing
      tileInfo.lakeDepth = LakeDepth_Medium;
      break;
    }
  }
}

// ----------------------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------------------

// ====================================================================================================
// UTILITY
// ====================================================================================================

// ----------------------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------------------

// Random code partially copied from blinklib because there's no function
// to set an explicit seed.
byte __attribute__((noinline)) randGetByte()
{
  // Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs"
  uint32_t x = randState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  randState = x;
  return x & 0xFF;
}

// ----------------------------------------------------------------------------------------------------

// MIN inclusive, MAX not inclusive
byte __attribute__((noinline)) randRange(byte min, byte max)
{
  byte val = randGetByte();
  byte range = max - min;
  uint16_t mult = val * range;
  return min + (mult >> 8);
}

// ====================================================================================================
// LOOP
// ====================================================================================================

void loop()
{
  sendOurTileInfo = false;
  
  // Save our current lake depth in case it changes this loop and we need to update our neighbors
  prevLakeDepth = tileInfo.lakeDepth;

  commReceive();
  detectNeighbors();

  handleUserInput();

  switch (tileInfo.tileType)
  {
    case TileType_Lake:
      loop_Lake();
      break;

    case TileType_Player:
      loop_Player();
      break;
  }

  if (tileInfo.tileType == TileType_Lake && tileInfo.lakeDepth != prevLakeDepth)
  {
    sendOurTileInfo = true;
  }

  if (sendOurTileInfo)
  {
    FOREACH_FACE(f)
    {
      enqueueCommOnFace(f, Command_TileType, tileInfo.tileType);
      if (tileInfo.tileType == TileType_Lake)
      {
        enqueueCommOnFace(f, Command_LakeDepth, tileInfo.lakeDepth);
      }
    }
  }

  commSend();

  render();
}

// ----------------------------------------------------------------------------------------------------

void detectNeighbors()
{
  // Detect all neighbors, taking optional action on neighbors added/removed
  numNeighborLakeTiles = 0;
  FOREACH_FACE(f)
  {
    if (isValueReceivedOnFaceExpired(f))
    {
      // No neighbor

      // Was neighbor just removed?
      if (neighborTileInfo[f].tileType != TileType_NotPresent)
      {
        // If we are a player tile then force recompute the casting info
        playerState = PlayerState_Idle;
      }

      neighborTileInfo[f].tileType = TileType_NotPresent;
    }
    else
    {
      // Neighbor present

      // Is this a new neighbor?
      if (neighborTileInfo[f].tileType == TileType_NotPresent)
      {
        // New neighbor
        neighborTileInfo[f].tileType = TileType_Unknown;
        determineOurLakeDepth();

        // If we are a player tile then force recompute the casting info
        playerState = PlayerState_Idle;

        // Force us to send out our tile info to the new neighbor
        sendOurTileInfo = true;
      }
      else if (neighborTileInfo[f].tileType == TileType_Lake)
      {
        numNeighborLakeTiles++;
      }
    }
  }

  if (numNeighborLakeTiles < FACE_COUNT)
  {
    // If we have an empty neighbor then we must be a shallow tile
    tileInfo.lakeDepth = LakeDepth_Shallow;
  }
  else if (tileInfo.lakeDepth == LakeDepth_Shallow)
  {
    // Fully surrounded - stop being shallow - need to compute our depth based on our neighbors
    tileInfo.lakeDepth = LakeDepth_Unknown;
    determineOurLakeDepth();
  }
}

// ----------------------------------------------------------------------------------------------------

void handleUserInput()
{
    // Swap between player tile and lake tile with triple click
  if (buttonMultiClicked() && buttonClickCount() == 3)
  {
    if (tileInfo.tileType == TileType_Lake)
    {
      resetPlayerTile();
    }
    else
    {
      resetLakeTile();
    }
    sendOurTileInfo = true;
    return;
  }

  if (buttonSingleClicked())
  {
    if (tileInfo.tileType == TileType_Player)
    {
      playerColorIndex++;
      if (playerColorIndex >= MAX_PLAYER_COLORS)
      {
        playerColorIndex = 0;
      }
    }
  }
}

// ----------------------------------------------------------------------------------------------------

void loop_Lake()
{
  tryToSpawnFish();
  moveFish();
}

// ----------------------------------------------------------------------------------------------------

void loop_Player()
{
  switch (playerState)
  {
    case PlayerState_Idle:
      {
        if (numNeighborLakeTiles > 0 && numNeighborLakeTiles <= 3)
        {
          // Let the player cast their line
          playerState = PlayerState_GetCastAngle;

          // Find any face with a lake neighbor
          FOREACH_FACE(f)
          {
            if (neighborTileInfo[f].tileType == TileType_Lake)
            {
              castFaceStart = castFaceEnd = f;
              break;
            }
          }

          // Go CCW from there looking for the first face
          FOREACH_FACE(f)
          {
            byte faceTest = CCW_FROM_FACE(castFaceStart, 1);
            if (neighborTileInfo[faceTest].tileType != TileType_Lake)
            {
              break;
            }
            castFaceStart = faceTest;
          }
          
          // Go CW from there looking for the last face
          FOREACH_FACE(f)
          {
            byte faceTest = CW_FROM_FACE(castFaceEnd, 1);
            if (neighborTileInfo[faceTest].tileType != TileType_Lake)
            {
              break;
            }
            castFaceEnd = faceTest;
          }

          // Got the face range - start sweeping side to side
          castSweepDirection = CastSweepDirection_CW;
          castFace = castFaceStart;
          castAngle = 0;
        }
      }
      break;

    case PlayerState_GetCastAngle:
      {
        if (buttonDown())
        {
          playerState = PlayerState_GetCastPower;
          castPower = 0;
          castSweepDirection = CastSweepDirection_Up;
        }
        else
        {
          if (castSweepDirection == CastSweepDirection_CW)
          {
            if (castAngle <= (255 - CAST_ANGLE_SWEEP_INC))
            {
              castAngle += CAST_ANGLE_SWEEP_INC;
            }
            else if (castFace != castFaceEnd)
            {
              castFace = CW_FROM_FACE(castFace, 1);
              castAngle += CAST_ANGLE_SWEEP_INC;
            }
            else
            {
              castAngle = 255;
              castSweepDirection = CastSweepDirection_CCW;
            }
          }
          else
          {
            if (castAngle >= CAST_ANGLE_SWEEP_INC)
            {
              castAngle -= CAST_ANGLE_SWEEP_INC;
            }
            else if (castFace != castFaceStart)
            {
              castFace = CCW_FROM_FACE(castFace, 1);
              castAngle -= CAST_ANGLE_SWEEP_INC;
            }
            else
            {
              castAngle = 0;
              castSweepDirection = CastSweepDirection_CW;
            }
          }
        }
      }
      break;

    case PlayerState_GetCastPower:
      {
        if (buttonReleased())
        {
          //playerState = PlayerState_Casting;
        }
        else
        {
          if (castSweepDirection == CastSweepDirection_Up)
          {
            if (castPower <= (255 - CAST_POWER_SWEEP_INC))
            {
              castPower += CAST_POWER_SWEEP_INC;
            }
            else if (castPower == 255)
            {
              castSweepDirection = CastSweepDirection_Down;
            }
            else
            {
              castPower = 255;
            }
          }
          else
          {
            if (castPower >= CAST_POWER_SWEEP_INC)
            {
              castPower -= CAST_POWER_SWEEP_INC;
            }
            else if (castPower == 0)
            {
              castSweepDirection = CastSweepDirection_Up;
            }
            else
            {
              castPower = 0;
            }
          }
        }
      }
      break;
  }
}

// ----------------------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------------------

// ====================================================================================================
// FISH
// ====================================================================================================

void resetSpawnTimer()
{
  byte spawnDelay = randRange(FISH_SPAWN_RATE_MIN, FISH_SPAWN_RATE_MAX);
  spawnTimer.set((spawnDelay + fishSpawnDelay)<<7);
}

// ----------------------------------------------------------------------------------------------------

void tryToSpawnFish()
{
  // Fish can only spawn on lake tiles with defined depth
  if (tileInfo.tileType != TileType_Lake)
  {
    return;
  }

  // Can't spawn fish until we know how deep we are
  if (tileInfo.lakeDepth == LakeDepth_Unknown)
  {
    resetSpawnTimer();
    return;
  }

  if (!spawnTimer.isExpired())
  {
    return;
  }
  resetSpawnTimer();

  // Can this tile spawn a fish?
  if (numFishToSpawn == 0)
  {
    return;
  }

  // Don't spawn if there's already a fish here
  for (byte fishIndex = 0; fishIndex < MAX_FISH_PER_TILE; fishIndex++)
  {
    if (residentFish[fishIndex].fishType != FishType_None)
    {
      return;
    }
  }

  // Spawn the fish in the first slot
  byte fishIndex = 0;

  numFishToSpawn--;
  residentFish[fishIndex].fishType = FishType_Small;
  residentFish[fishIndex].curFace = randRange(0, 6);
  assignFishMoveTarget(fishIndex);
  residentFish[fishIndex].moveTimer.set(FISH_MOVE_RATE_PAUSE);

  // Tell the entire lake we just spawned a fish so they delay spawning theirs
  // Help control the fish population...
  broadcastCommToAllNeighbors(Command_FishSpawned, DONT_CARE);
  broadcastDelay.set(BROADCAST_DELAY_RATE);
}

// ----------------------------------------------------------------------------------------------------

void assignFishMoveTarget(byte fishIndex)
{
  // Figure out which neighbors are present, are lake tiles, and are of the same depth as us
  byte numValidTargetTiles = 0;
  FOREACH_FACE(f)
  {
    if (neighborTileInfo[f].tileType == TileType_Lake &&
        neighborTileInfo[f].lakeDepth == tileInfo.lakeDepth)
      {
        fishMoveTargetTiles[numValidTargetTiles] = f;
        numValidTargetTiles++;
      }
  }

  // Once here, fishMoveTargetTiles[] will contain a list of faces that point to valid move targets.
  // The array can contain from zero to six valid face numbers. The final value of numValidTargetTiles
  // tells us the length of the array.
  //
  // For instance, a tile with three valid neighbors might result in an array of { 0, 4, 5 }
  // with numValidTargetTiles=3
  // 
  // A tile surrounded by zero valid tiles will have an empty array and numValidTargetTiles=0

  // 50/50 chance of leaving for a neighbor tile
  byte leaveTile = randRange(0, 2);

  if (leaveTile && numValidTargetTiles > 0)
  {
    // Pick a valid target at random
    byte randTargetTileIndex = randRange(0, numValidTargetTiles);
    residentFish[fishIndex].destNeighbor = fishMoveTargetTiles[randTargetTileIndex];
  }
  else
  {
    residentFish[fishIndex].destNeighbor = INVALID_FACE;
  }

  // Target face is just random
  residentFish[fishIndex].destFace = randRange(0, 6);
}

// ----------------------------------------------------------------------------------------------------

void moveFish()
{
  for (byte fishIndex = 0; fishIndex < MAX_FISH_PER_TILE; fishIndex++)
  {
    if (residentFish[fishIndex].fishType == FishType_None)
    {
      // No fish in this slot
      continue;
    }

    if (!residentFish[fishIndex].moveTimer.isExpired())
    {
      // Fish in this slot is not ready to move yet
      continue;
    }
    residentFish[fishIndex].moveTimer.set(FISH_MOVE_RATE_NORMAL);

    // If the fish is at the target then pick a new one
    if (residentFish[fishIndex].destNeighbor == INVALID_FACE &&
        residentFish[fishIndex].destFace == residentFish[fishIndex].curFace)
    {
      assignFishMoveTarget(fishIndex);
      residentFish[fishIndex].moveTimer.set(FISH_MOVE_RATE_PAUSE);
      return;
    }

    // If the fish is next to the tile it wants to move into, send a comm trying to make the transfer
    if (residentFish[fishIndex].destNeighbor == residentFish[fishIndex].curFace)
    {
      // Make the destination neighbor face invalid.
      // This way, if the neighbor tile cannot accept this fish, or just fails to respond, the fish
      // will continue moving within the current tile as if nothing happened.
      // Corner case: If the destination tile is slow to respond then this fish may make a move
      // within the tile before being removed. We'll need to adjust the fish movement speed to be
      // slow enough that this doesn't happen much. Same thing happened in Terrarium when moving
      // critters between tiles.
      residentFish[fishIndex].destNeighbor = INVALID_FACE;
      enqueueCommOnFace(residentFish[fishIndex].curFace, Command_FishTransfer, fishIndex);
      return;
    }

    // Move the fish closer to its target
    // Either the face within the tile, or to the neighbor tile
    byte destFaceInThisTile = residentFish[fishIndex].destFace;
    if (residentFish[fishIndex].destNeighbor != INVALID_FACE)
    {
      destFaceInThisTile = residentFish[fishIndex].destNeighbor;
    }

    // Use our handy-dandy hard-coded array to tell us where the fish should move given its current 
    // face and its destination face.
    byte nextFace = nextMoveFaceArray[destFaceInThisTile][residentFish[fishIndex].curFace];
    residentFish[fishIndex].curFace = nextFace;
  }
}

// ====================================================================================================
// RENDER
// ====================================================================================================

void render()
{
  Color color;
  setColor(OFF);

  switch (tileInfo.tileType)
  {
    case TileType_Lake:
      {
        switch (tileInfo.lakeDepth)
        {
          case LakeDepth_Shallow: setColor(RED); break;
          case LakeDepth_Medium: setColor(GREEN); break;
          case LakeDepth_Deep: setColor(BLUE); break;
          case LakeDepth_Unknown: setColor(WHITE); break;
          default: setColor(WHITE); break;
        }

        for (byte fishIndex = 0; fishIndex < MAX_FISH_PER_TILE; fishIndex++)
        {
          if (residentFish[fishIndex].fishType != FishType_None)
          {
            setFaceColor(residentFish[fishIndex].curFace, WHITE);
          }
        }
      }
      break;

    case TileType_Player:
      {
        color.as_uint16 = playerColors[playerColorIndex];
        switch (playerState)
        {
          case PlayerState_Idle:
            setFaceColor(0, color);
            setFaceColor(3, WHITE);
            break;

          case PlayerState_GetCastAngle:
            {
              //setFaceColor(castFaceStart, BLUE);
              //setFaceColor(castFaceEnd, YELLOW);

              // Light up the face in the direction the player is casting
              setFaceColor(castFace, WHITE);

              // The player's color goes on the opposite face
              byte otherFace = CW_FROM_FACE(castFace, 3);
              color.as_uint16 = playerColors[playerColorIndex];
              setFaceColor(otherFace, color);
            }
            break;

          case PlayerState_GetCastPower:
            {
              setFaceColor(castFace, WHITE);

              byte otherFace = CW_FROM_FACE(castFace, 3);
              color.as_uint16 = playerColors[playerColorIndex];
              setFaceColor(otherFace, color);

              if (castPower < 64)
              {
                byte otherFace = CW_FROM_FACE(castFace, 3);
                setFaceColor(otherFace, YELLOW);
              }
              else if (castPower < 128)
              {
                byte otherFace = CW_FROM_FACE(castFace, 2);
                setFaceColor(otherFace, YELLOW);
                otherFace = CCW_FROM_FACE(castFace, 2);
                setFaceColor(otherFace, YELLOW);
              }
              else if (castPower < 192)
              {
                byte otherFace = CW_FROM_FACE(castFace, 1);
                setFaceColor(otherFace, YELLOW);
                otherFace = CCW_FROM_FACE(castFace, 1);
                setFaceColor(otherFace, YELLOW);
              }
              else
              {
                setFaceColor(castFace, YELLOW);
              }
            }
            break;
        }
      }
      break;

    default:
      setColor(WHITE);
      break;
  }

  FOREACH_FACE(f)
  {
    if (commInsertionIndexes[f] == COMM_INDEX_ERROR_OVERRUN)
    {
      setColorOnFace(MAGENTA, f);
    }
  }
}
