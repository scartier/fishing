#include <commlib.h>

byte faceOffsetArray[] = { 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5 };

#define CCW_FROM_FACE(f, amt) faceOffsetArray[6 + (f) - (amt)]
#define CW_FROM_FACE(f, amt)  faceOffsetArray[(f) + (amt)]
#define OPPOSITE_FACE(f)      CW_FROM_FACE((f), 3)
#define RGB_TO_U16(r,g,b) ((((uint16_t)(r)>>3) & 0x1F)<<1 | (((uint16_t)(g)>>3) & 0x1F)<<6 | (((uint16_t)(b)>>3) & 0x1F)<<11)

#define DONT_CARE 0

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

struct TileInfo
{
  TileType tileType;
  LakeDepth lakeDepth;
};

TileInfo tileInfo;
LakeDepth prevLakeDepth;
TileInfo neighborTileInfo[FACE_COUNT];
bool sendOurTileInfo = false;

// ----------------------------------------------------------------------------------------------------

byte numNeighborLakeTiles = 0;
bool transmitNewDepth = false;

// ----------------------------------------------------------------------------------------------------

enum Command : uint8_t
{
  Command_SendTileType,
  Command_SendLakeDepth,
};

// ====================================================================================================

void setup()
{
  reset();
}

// ----------------------------------------------------------------------------------------------------

void reset()
{
  tileInfo.tileType = TileType_Lake;
  tileInfo.lakeDepth = LakeDepth_Shallow;
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
    case Command_SendTileType:
      neighborTileInfo[f].tileType = (TileType) value;
      if (neighborTileInfo[f].tileType == TileType_Lake)
      {
        // Expect the next comm to be the neighbor's depth
        neighborTileInfo[f].lakeDepth = LakeDepth_Unknown;
      }
      break;

    case Command_SendLakeDepth:
      processNeighborLakeDepth(f, (LakeDepth) value);
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
      enqueueCommOnFace(f, Command_SendTileType, tileInfo.tileType);
      if (tileInfo.tileType == TileType_Lake)
      {
        enqueueCommOnFace(f, Command_SendLakeDepth, tileInfo.lakeDepth);
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

void loop_Lake()
{
}

// ----------------------------------------------------------------------------------------------------

void loop_Player()
{
}

// ----------------------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------------------

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
      }
      break;

    case TileType_Player:
      setColor(WHITE);
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
