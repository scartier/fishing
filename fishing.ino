#include <commlib.h>

#define ENABLE_LAKE_TILE
//#define ENABLE_FISHERFOLK_TILE

byte faceOffsetArray[] = { 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5 };

#define CCW_FROM_FACE(f, amt) faceOffsetArray[6 + (f) - (amt)]
#define CW_FROM_FACE(f, amt)  faceOffsetArray[(f) + (amt)]
#define OPPOSITE_FACE(f)      CW_FROM_FACE((f), 3)
#define RGB_TO_U16(r,g,b) ((((uint16_t)(r)>>3) & 0x1F)<<1 | (((uint16_t)(g)>>3) & 0x1F)<<6 | (((uint16_t)(b)>>3) & 0x1F)<<11)

#define DONT_CARE 0
#define INVALID_FACE 7

#define setFaceColor(x,y) setColorOnFace(y,x)

#define USE_DATA_SPONGE 0

#if USE_DATA_SPONGE
#warning DATA SPONGE ENABLED
byte sponge[220];
// Nov 12 2021 : ~210
#endif

#define NO_INLINE __attribute__((noinline))
//#define NO_INLINE

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

// ----------------------------------------------------------------------------------------------------
// LAKE

#ifdef ENABLE_LAKE_TILE

#define LAKE_COLOR_SHALLOW  RGB_TO_U16(  0, 128, 128 )
#define LAKE_COLOR_MEDIUM   RGB_TO_U16(  0,   0, 128 )
#define LAKE_COLOR_DEEP     RGB_TO_U16(  0,   0,  64 )

enum LakeDepth : uint8_t
{
  LakeDepth_Unknown,
  LakeDepth_Shallow,
  LakeDepth_Medium,
  LakeDepth_Deep,
};

uint16_t WaterColors[] =
{
  0,
  LAKE_COLOR_SHALLOW,
  LAKE_COLOR_MEDIUM,
  LAKE_COLOR_DEEP
};

#endif  // ENABLE_LAKE_TILE

// ----------------------------------------------------------------------------------------------------
// FISH

#ifdef ENABLE_LAKE_TILE

#define FISH_SPAWN_RATE_MIN (5000>>7)
#define FISH_SPAWN_RATE_MAX (15000>>7)
#define CUMULATIVE_FISH_SPAWN_DELAY 10000
byte numFishToSpawn = 1;
Timer spawnTimer;
uint32_t fishSpawnDelay = 0;
byte fishMoveTargetTiles[FACE_COUNT];

#define BROADCAST_DELAY_RATE 1000
Timer broadcastDelay;

#endif  // ENABLE_LAKE_TILE

enum FishSize : uint8_t
{
  FishSize_None,
  FishSize_Small,
  FishSize_Medium,
  FishSize_Large,
};

struct FishType
{
  FishSize size : 2;
  byte evasion  : 2;
  byte UNUSED   : 4;  // do not use - keep data at 4 bits to fit in a comm packet
};

union FishTypeUnion
{
  FishType type;
  uint8_t rawBits;
};

#ifdef ENABLE_LAKE_TILE

struct FishInfo
{
  FishTypeUnion fishType;
  byte evasion_UNUSED;
  byte curFace;
  Timer moveTimer;
  byte destFace;
  byte destNeighbor;
};

#define FISH_MOVE_RATE_NORMAL 800
#define FISH_MOVE_RATE_PAUSE 3000

#define MAX_FISH_PER_TILE 3
#define INVALID_FISH (MAX_FISH_PER_TILE+1)
FishInfo residentFish[MAX_FISH_PER_TILE];

#endif  // ENABLE_LAKE_TILE

// ----------------------------------------------------------------------------------------------------
// FISH VISUALS

#ifdef ENABLE_FISHERFOLK_TILE

enum FishDisplay : uint8_t
{
  FishDisplay_None,

  // Small Fish: Single face
  FishDisplay_Sm1,        // single tail
  FishDisplay_Sm2Split,   // 2 face split tail
  FishDisplay_Sm2Big,     // 2 face big tail

  // Medium Fish: Two faces
  FishDisplay_Md1,        // single tail
  FishDisplay_Md2Wave,    // 1-2 face tail up and down
  FishDisplay_Md2Split,   // 2 face tail split
  FishDisplay_Md3Wave,    // 2-3 face tail up and down
  FishDisplay_Md4Circle,  // single tail in a circular motion

  // Large Fish: Three+ faces
  FishDisplay_Lg1,        // three faces - single tail
  FishDisplay_Lg2,        // three faces - large tail
  FishDisplay_Lg2Ripple,  // four faces - color ripples across body

  // Special
  FishDisplay_Eel,        // special: three faces that move in a line around the tile
  FishDisplay_Puffer,     // special: two opposite faces - puffs into all six faces
  FishDisplay_Strike,     // one face - quickly strikes and then retreats

  FishDisplay_MAX
};

FishDisplay FishTypeToDisplayOptions[3][7] =
{
  // Shallow
  {
    FishDisplay_Sm1,        // evasion 1
    FishDisplay_Sm1,        // evasion 1
    FishDisplay_Md1,        // evasion 1-2
    FishDisplay_Md1,        // evasion 2
    FishDisplay_Sm2Split,   // evasion 2-3
    FishDisplay_Sm2Big,     // evasion 3
    FishDisplay_Md2Wave     // evasion 3
  },

  // Medium
  {
    FishDisplay_Md1,        // evasion 1
    FishDisplay_Md2Wave,    // evasion 1
    FishDisplay_Md2Split,   // evasion 1-2
    FishDisplay_Md3Wave,    // evasion 2
    FishDisplay_Md4Circle,  // evasion 2-3
    FishDisplay_Eel,        // evasion 3
    FishDisplay_Strike,     // evasion 3
  },

  // Deep
  {
    FishDisplay_Md3Wave,    // evasion 1
    FishDisplay_Lg1,        // evasion 1
    FishDisplay_Lg2,        // evasion 1-2
    FishDisplay_Lg2Ripple,  // evasion 2
    FishDisplay_Puffer,     // evasion 2-3
    FishDisplay_Eel,        // evasion 3
    FishDisplay_Strike,     // evasion 3
  }
};

#define END_OF_ANIM 0xFF
struct AnimFrameInfo
{
  byte bitwiseColor1;
  byte bitwiseColor2;
  byte delay;
};
AnimFrameInfo fishDisplayAnimFrames[] =
{
  // ============================
  // SMALL FISH (one face + tail)
  // ============================

  // Sm1
  { 0b000001, 0b000100, 5 }, { 0b000001, 0b001000, 5 },
  { 0b000001, 0b010000, 5 }, { 0b000001, 0b001000, 5 },
  { END_OF_ANIM },

  // Sm2Split
  { 0b000001, 0b010100, 10 }, { 0b000001, 0b001000, 10 },
  { END_OF_ANIM },

  // Sm2Split
  { 0b000001, 0b001100, 10 }, { 0b000001, 0b011000, 10 },
  { END_OF_ANIM },


  // =============================
  // MEDIUM FISH (two faces + tail)
  // =============================

  // Md1
  { 0b000011, 0b001000, 10 }, { 0b000011, 0b010000, 10 },
  { END_OF_ANIM },

  // Md2Wave
  { 0b000011, 0b110000, 10 }, { 0b000011, 0b010000, 5 },
  { 0b000011, 0b011000, 10 }, { 0b000011, 0b001000, 5 },
  { 0b000011, 0b001100, 10 }, { 0b000011, 0b001000, 5 },
  { 0b000011, 0b011000, 10 }, { 0b000011, 0b010000, 5 },
  { END_OF_ANIM },

  // Md2Split
  { 0b000011, 0b100100, 5 }, { 0b000011, 0b011000, 10 },
  { END_OF_ANIM },

  // Md3Wave
  { 0b000011, 0b001100, 10 }, { 0b000011, 0b111000, 5 },
  { 0b000011, 0b110000, 10 }, { 0b000011, 0b011100, 5 },
  { END_OF_ANIM },

  // Md4Circle
  { 0b000011, 0b000100, 5 }, { 0b000011, 0b001000, 5 },
  { 0b000011, 0b010000, 5 }, { 0b000011, 0b100000, 5 },
  { END_OF_ANIM },

  // ================================
  // LARGE FISH (three+ faces + tail)
  // ================================

  // Lg1
  { 0b000111, 0b010000, 10 }, { 0b000111, 0b100000, 10 },
  { 0b000111, 0b010000, 10 }, { 0b000111, 0b001000, 10 },
  { END_OF_ANIM },

  // Lg2
  { 0b000111, 0b110000, 5 }, { 0b000111, 0b011000, 5 },
  { END_OF_ANIM },

  // Lg2Ripple
  { 0b001111, 0b000000, 100 },
  { 0b001001, 0b000110, 10 }, { 0b000110, 0b001001, 10 }, { 0b000110, 0b110000, 10 },
  { END_OF_ANIM },


  // ==========================
  // SPECIAL (not typical fish)
  // ==========================

  // Eel
  { 0b000010, 0b000101, 50 }, { 0b001010, 0b000101, 10 },
  { 0b001010, 0b000100, 50 }, { 0b001010, 0b010100, 10 },
  { 0b001000, 0b010100, 50 }, { 0b101000, 0b010100, 10 },
  { 0b101000, 0b010000, 50 }, { 0b101000, 0b010001, 10 },
  { 0b100000, 0b010001, 50 }, { 0b100010, 0b010001, 10 },
  { 0b100010, 0b000001, 50 }, { 0b100010, 0b000101, 10 },
  { END_OF_ANIM },

  // Puffer
  { 0b001001, 0b000000, 100 },
  { 0b001001, 0b110110, 200 },
  { END_OF_ANIM },

  // Strike
  { 0b000001, 0b000000, 200 },
  { 0b000010, 0b000001, 2 }, { 0b000101, 0b000010, 2 }, { 0b001010, 0b000101, 2 },
  { 0b010101, 0b001010, 30 },
  { 0b001010, 0b000101, 2 }, { 0b000101, 0b000010, 2 }, { 0b000010, 0b000001, 2 },
  { END_OF_ANIM },

};
byte fishDisplayAnimStartIndexes[FishDisplay_MAX];

#define CAUGHT_FISH_IN_BAG_DIM 192

struct CaughtFishInfo
{
  FishDisplay display;
  uint16_t color1;
  uint16_t color2;
};

#define MAX_CAUGHT_FISH 6
CaughtFishInfo caughtFishInfo[MAX_CAUGHT_FISH] =
{
  // { FishDisplay_Md3Wave, RGB_TO_U16(128, 64, 0), RGB_TO_U16(128, 128, 0) },
  // { FishDisplay_Lg1, RGB_TO_U16(128, 64, 0), RGB_TO_U16(128, 128, 0) },
  // { FishDisplay_Lg2,    RGB_TO_U16(128, 64, 0), RGB_TO_U16(128, 128, 0) },
  // { FishDisplay_Lg2Ripple, RGB_TO_U16(128, 64, 0), RGB_TO_U16(128, 128, 0) },
  // { FishDisplay_Md4Circle, RGB_TO_U16(128, 64, 0), RGB_TO_U16(128, 128, 0) },
  // { FishDisplay_Md2Wave, RGB_TO_U16(128, 64, 0), RGB_TO_U16(128, 128, 0) },
};
byte newlyCaughtFishIndex = MAX_CAUGHT_FISH;

// Fish animation display
byte fishDisplayingIndex = MAX_CAUGHT_FISH;
byte fishAnimFrame = 0;
Timer fishAnimTimer;

#endif // ENABLE_FISHERFOLK_TILE

// ----------------------------------------------------------------------------------------------------
// FISH MOVEMENT

#ifdef ENABLE_LAKE_TILE

// Handy dandy array telling us where a fish should move next given its current face and its desired destination
byte nextMoveFaceArray[6][6] =
{
  { 0, 0, 1, 2, 5, 0 }, // Dest face 0 - 50/50 CCW
  { 1, 1, 1, 2, 5, 0 }, // Dest face 1 - 50/50 CW
  { 1, 2, 2, 2, 3, 4 }, // Dest face 2 - 50/50 CCW
  { 1, 2, 3, 3, 3, 4 }, // Dest face 3 - 50/50 CW
  { 5, 0, 3, 4, 4, 4 }, // Dest face 4 - 50/50 CCW
  { 5, 0, 3, 4, 5, 5 }, // Dest face 5 - 50/50 CW
};

// TODO : Why is the tug info on the lake tile and not the fisherfolk tile?
//        Won't this break with multiple lines cast in the same tile?
byte tugFace = INVALID_FACE;   // neighbor face that originated a tug
#define TUG_REPEAT_RATE 2000    // minimum time between tugs or else fish get scared
Timer tugRepeatTimer;
bool tugScare = false;

#endif  // ENABLE_LAKE_TILE

// ----------------------------------------------------------------------------------------------------
// TILE

// TODO : Can this be a single byte? Does that lead to larger code?
struct TileInfo
{
  TileType tileType;
  byte lakeDepth;
};

TileInfo tileInfo;
TileInfo neighborTileInfo[FACE_COUNT];
byte numNeighborLakeTiles = 0;

// ----------------------------------------------------------------------------------------------------
// PLAYER

#ifdef ENABLE_FISHERFOLK_TILE

#define MAX_PLAYER_COLORS 7
uint16_t playerColors[] =
{
  RGB_TO_U16( 160,  32,   0 ),  // red
  RGB_TO_U16(   0, 192, 124 ),  // aquamarine
  RGB_TO_U16( 224, 160,   0 ),  // orange
  RGB_TO_U16( 112, 212,   0 ),  // greenish-yellow
  RGB_TO_U16( 216,   0,  92 ),  // pink
  RGB_TO_U16(  16, 184,  32 ),  // sea green
  RGB_TO_U16( 108,   0, 178 ),  // purple
};

byte playerColorIndex = 0;

enum PlayerState
{
  PlayerState_Idle,           // detached from lake
  PlayerState_GetCastAngle,   // sweeping left/right waiting for player to click
  PlayerState_GetCastPower,   // sweeping forward/back waiting for player to release
  PlayerState_WaitingForFish, // line has been cast - waiting for player to catch fish or line break
  PlayerState_FishCaught,     // fish was just caught - fancy anim
  PlayerState_MustDisconnect, // player must disconnect from the lake to go back to idle
  PlayerState_ReleasingFish,  // discarding the fish (toss it back in the lake)
};
PlayerState playerState = PlayerState_Idle;

// Timer used to show different animations
#define PLAYER_ANIM_DELAY_IDLE1 (200>>4)
#define PLAYER_ANIM_DELAY_IDLE2 (2000>>4)
byte playerAnimStageDelays_FishCaught[] = { 200>>4, 200>>4, 500>>4 };
byte playerAnimStage;
byte playerAnimStep;
Timer playerAnimTimer;

#endif // ENABLE_FISHERFOLK_TILE

// ----------------------------------------------------------------------------------------------------
// CASTING

#ifdef ENABLE_FISHERFOLK_TILE

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

#endif // ENABLE_FISHERFOLK_TILE

#define FISH_STRUGGLE_DURATION  2000
#define FISH_STRUGGLE_DELAY     6000

struct CastInfo
{
  byte faceIn;            // the face where the line enters from the player
  byte faceOut;           // the face where the line exits towards the lake (INVALID if there is no exit)
  FishTypeUnion fishType; // type of fish on the hook
  bool escaping;          // the fish is trying to escape (tug to cancel before timer expires)
  Timer escapeTimer;      // time until fish escapes
};
#define MAX_CASTS_PER_TILE 4
#define INVALID_CAST 15
#ifdef ENABLE_LAKE_TILE
CastInfo castInfo[MAX_CASTS_PER_TILE];
#endif
#ifdef ENABLE_FISHERFOLK_TILE
CastInfo playerCastInfo;
#endif

#define MAX_CAST_ANGLES 5
#define MAX_CAST_STEPS 3
#define MAX_CAST_POWERS 4

#ifdef ENABLE_LAKE_TILE

enum CastStep
{
  CastStep_Left     = 2,    // the value is the number of faces CW from the entry face
  CastStep_Straight = 3,
  CastStep_Right    = 4,
};

CastStep castSteps[MAX_CAST_ANGLES][MAX_CAST_STEPS] =
{
  { CastStep_Left,      CastStep_Right,     CastStep_Left     },
  { CastStep_Straight,  CastStep_Left,      CastStep_Right    },
  { CastStep_Straight,  CastStep_Straight,  CastStep_Straight },
  { CastStep_Straight,  CastStep_Right,     CastStep_Left     },
  { CastStep_Right,     CastStep_Left,      CastStep_Right    }
};

#endif  // ENABLE_LAKE_TILE

// This 2D array is used when the player casts their line at a specific angle and power
byte powerToAngle[MAX_CAST_POWERS][MAX_CAST_ANGLES] =
{
  { 255, 255, 255, 255, 255 },  // POWER=0 - doesn't matter which one is selected
  {  64,  64, 192, 192, 255 },  // POWER=1 - skip Command_CastDir1 & Command_CastDir3
  {  64,  64, 192, 192, 255 },  // POWER=2 - skip Command_CastDir1 & Command_CastDir3
  {  32,  96, 160, 224, 255 },  // POWER=3
};


#ifdef ENABLE_FISHERFOLK_TILE

#define REEL_BUTTON_DURATION 1000
Timer reelTimer;

#endif // ENABLE_FISHERFOLK_TILE

// ----------------------------------------------------------------------------------------------------

#ifdef ENABLE_LAKE_TILE

#define SHORE_WAVE_RATE 10000
Timer shoreWaveTimer;

#define SHORE_WAVE_FADE_RATE 2
byte shoreWaveValue = 0;

#endif  // ENABLE_LAKE_TILE

// ----------------------------------------------------------------------------------------------------

#ifdef ENABLE_LAKE_TILE

#define RIPPLE_FADE_RATE 4
byte rippleValue = 0;
byte rippleFace = INVALID_FACE;   // INVALID_FACE means we originated the ripple

#endif  // ENABLE_LAKE_TILE

// ----------------------------------------------------------------------------------------------------

enum LineAction : uint8_t
{
  LineAction_Break,         // Sent when the line breaks for some reason
  LineAction_Tug,           // Player clicked tile to tug the line
  LineAction_Reel,          // Player is holding the button down to reel in the line
  LineAction_DoneReeling,   // End of the line sends this back to tell previous tile it is now the end
};

enum LakeFX : uint8_t
{
  LakeFX_ShoreWave,         // Periodic wave that laps on the shore tiles
  LakeFX_Ripple,            // Wave that ripples out when a cast drops in (or the player clicks the lake)
  LakeFX_Lure,              // When player tugs the line, causes a ripple and possibly luring nearby fish closer
};

enum Command : uint8_t
{
  Command_TileInfo,         // Tile type and lake depth for this tile
  Command_FishSpawned,      // Broadcast out when a tile spawns a fish - temporarily inhibits spawning in other tiles
  Command_FishTransfer,     // Sent when one tile is trying to transfer a fish to a neighbor
  Command_FishAccepted,     // Response indicating the transfer was accepted

  Command_CastDir0,
  Command_CastDir1,
  Command_CastDir2,
  Command_CastDir3,
  Command_CastDir4,

  Command_LineAction,       // See the 'LineAction' enum above for possible actions
  Command_FishHooked,       // Sent from the end of the cast back to the player to give the fish info

  // Effects
  Command_LakeFX,           // See the 'LakeFX' enum above for possible effects
};

// ====================================================================================================

void setup()
{
  #if USE_DATA_SPONGE
  // Use our data sponge so that it isn't compiled away
  if (sponge[0])
  {
    sponge[0] = 3;
  }
  #endif

  // Temporary random seed is generated detiministically from our tile's serial number
  uint32_t serial_num_32 = 2463534242UL;    // We start with  Marsaglia's seed...
  // ... and then fold in bits from our serial number
  for( byte i=0; i< SERIAL_NUMBER_LEN; i++ )
  {
    serial_num_32 ^=  getSerialNumberByte(i) << (i * 3);
  }

  randState=serial_num_32;

#ifdef ENABLE_LAKE_TILE
  resetLakeTile();
#else
  resetPlayerTile();
#endif
}

// ----------------------------------------------------------------------------------------------------

void resetLakeTile()
{
#ifdef ENABLE_LAKE_TILE
  tileInfo.tileType = TileType_Lake;
  tileInfo.lakeDepth = LakeDepth_Unknown;

  numFishToSpawn = 1;
  resetSpawnTimer();

  // Reset the list of fish in this tile
  for (byte fish = 0; fish < MAX_FISH_PER_TILE; fish++)
  {
    residentFish[fish].fishType.type.size = FishSize_None;
  }

  // Reset the list of casts through this tile
  for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
  {
    castInfo[castIndex].faceIn = INVALID_FACE;
  }
#endif  // ENABLE_LAKE_TILE
}

// ----------------------------------------------------------------------------------------------------

#ifdef ENABLE_FISHERFOLK_TILE
void resetPlayerTile()
{
  tileInfo.tileType = TileType_Player;
  playerState = PlayerState_MustDisconnect;

  // Find the start of all the fish animations
  // Do this at runtime to avoid human error of doing it manually
  fishDisplayAnimStartIndexes[0] = 0;
  fishDisplayAnimStartIndexes[1] = 0;
  int frameIndex = 0;
  for (int fishDisplayIndex = 2; fishDisplayIndex < FishDisplay_MAX; fishDisplayIndex++)
  {
    while (fishDisplayAnimFrames[frameIndex].bitwiseColor1 != END_OF_ANIM)
    {
      frameIndex++;
    }
    frameIndex++;
    fishDisplayAnimStartIndexes[fishDisplayIndex] = frameIndex++;
  }
}
#endif  // ENABLE_FISHERFOLK_TILE

// ====================================================================================================
// COMMUNICATION
// ====================================================================================================

// ----------------------------------------------------------------------------------------------------

void processCommForFace(byte commandByte, byte value, byte f)
{
  Command command = (Command) commandByte;

  switch (command)
  {
    case Command_TileInfo:
    {
      TileType newTileType = (TileType) (value & 0x3);
#ifdef ENABLE_FISHERFOLK_TILE
      if (neighborTileInfo[f].tileType != newTileType && newTileType == TileType_Lake)
      {
        // If a lake neighbor was added, force the player to recompute casting info
        playerState = PlayerState_Idle;
        breakLine(f);
      }
#endif
      neighborTileInfo[f].tileType  = newTileType;
      neighborTileInfo[f].lakeDepth = value >> 2;
    }
      break;

#ifdef ENABLE_LAKE_TILE

    case Command_FishSpawned:
      if (broadcastDelay.isExpired())
      {
        // Cummulative delay for each subsequent fish spawn
        fishSpawnDelay += CUMULATIVE_FISH_SPAWN_DELAY;

        broadcastCommToAllNeighbors(Command_FishSpawned, DONT_CARE);
        resetSpawnTimer();
        broadcastDelay.set(BROADCAST_DELAY_RATE);
      }
      break;

    case Command_FishTransfer:
      // Neighbor tile is trying to send us their fish.

      // Check if we have room for this fish
      for (byte fishIndex = 0; fishIndex < MAX_FISH_PER_TILE; fishIndex++)
      {
        FishType fishType = residentFish[fishIndex].fishType.type;
        if (fishType.size == FishSize_None)
        {
          // Found an empty slot for a fish
          // Accept the fish and assign it a random destination
          enqueueCommOnFace(f, Command_FishAccepted, value);
          residentFish[fishIndex].fishType.rawBits = value;
          residentFish[fishIndex].curFace = f;
          assignFishMoveTarget(fishIndex);
          residentFish[fishIndex].moveTimer.set(FISH_MOVE_RATE_NORMAL - 100 * fishType.evasion);
          break;
        }
      }
      break;

    case Command_FishAccepted:
      {
        // Neighbor tile accepted our request to give them our fish
        // Remove the fish from our list
        byte fishMatchIndex = INVALID_FISH;
        for (byte fishIndex = 0; fishIndex < MAX_FISH_PER_TILE; fishIndex++)
        {
          if (residentFish[fishIndex].fishType.rawBits == value)
          {
            fishMatchIndex = fishIndex;
            if (residentFish[fishIndex].curFace == f)
            {
              // Found a matching fish within the face we are expecting it to be - donezo
              break;
            }
          }
        }
        if (fishMatchIndex != INVALID_FISH)
        {
          residentFish[fishMatchIndex].fishType.type.size = FishSize_None;
        }
      }
      break;

    case Command_CastDir0:
    case Command_CastDir1:
    case Command_CastDir2:
    case Command_CastDir3:
    case Command_CastDir4:
      // Capture the casting info
      {
        byte castDir = command - Command_CastDir0;
        byte stepsToGo = value & 0x3;
        byte currentStep = value >> 2;
        byte castFaceOut = INVALID_FACE;
        if (stepsToGo > 0)
        {
          byte castStepDir = castSteps[castDir][currentStep];
          castFaceOut = CW_FROM_FACE(f, castStepDir);
        }

        // First check if this is already used for another cast
        // By only allowing a single line to use a face we simplify some comm packets
        bool breakLine = false;
        for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
        {
          // Check if face in matches any casts
          if (castInfo[castIndex].faceIn == f)
          {
            breakLine = true;
          }
          else if (castInfo[castIndex].faceIn != INVALID_FACE && castInfo[castIndex].faceOut == f)
          {
            breakLine = true;
          }

          // Check if face out matches any casts
          if (castFaceOut != INVALID_FACE)
          {
            if (castInfo[castIndex].faceIn == castFaceOut)
            {
              // Face in is used by another cast
              breakLine = true;
            }
            else if (castInfo[castIndex].faceIn != INVALID_FACE && castInfo[castIndex].faceOut == castFaceOut)
            {
              // Face out is used by another cast
              breakLine = true;
            }
          }
        }
        // Can't reuse a face for two casts
        if (!breakLine)
        {
          // Now try to find room and store the new cast

          // Start assuming we won't find a slot
          breakLine = true;

          for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
          {
            if (castInfo[castIndex].faceIn == INVALID_FACE)
            {
              // Found an empty slot - use it for this cast
              breakLine = false;
              castInfo[castIndex].faceIn = f;
              castInfo[castIndex].faceOut = INVALID_FACE; // assume we are done stepping
              castInfo[castIndex].fishType.type.size = FishSize_None;

              byte castDir = command - Command_CastDir0;
              byte stepsToGo = value & 0x3;
              byte currentStep = value >> 2;
              if (stepsToGo > 0)
              {
                byte castStepDir = castSteps[castDir][currentStep];
                byte castFaceOut = CW_FROM_FACE(f, castStepDir);

                // Make sure the out face isn't used by another cast


                castInfo[castIndex].faceOut = castFaceOut;

                if (neighborTileInfo[castFaceOut].tileType != TileType_Lake)
                {
                  // Break line if there isn't a tile to go to
                  breakLine = true;
                  castInfo[castIndex].faceIn = INVALID_FACE;
                }
                else
                {
                  // Continue the casting to the next tile
                  stepsToGo--;
                  currentStep++;
                  byte castData = (currentStep << 2) | stepsToGo;
                  enqueueCommOnFace(castFaceOut, command, castData);  // same command since it encodes the direction
                }
              }
              else
              {
                // Dropping the line here - ripple out
                startRipple(false);
              }
              break;
            }
          }
        }
        if (breakLine)
        {
          // Too many lines passing through here
          // Tell the new one to go away
          enqueueCommOnFace(f, Command_LineAction, LineAction_Break);
        }
      }
      break;

#endif  // ENABLE_LAKE_TILE

    case Command_LineAction:
      {
        switch (value)
        {
          case LineAction_Break:
            breakLine(f);
            break;

#ifdef ENABLE_LAKE_TILE
          case LineAction_Tug:
          case LineAction_Reel:
            for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
            {
              FishType fishType = castInfo[castIndex].fishType.type;

              if (castInfo[castIndex].faceIn == f)
              {
                if (castInfo[castIndex].faceOut == INVALID_FACE)
                {
                  // End of the line - do the tug/reel
                  if (value == LineAction_Tug)
                  {
                    if (fishType.size == FishSize_None)
                    {
                      startRipple(true);
                    }
                    else
                    {
                      // If a fish is escaping and we just tugged then CALM DOWN FISH JEEZ
                      if (castInfo[castIndex].escaping)
                      {
                        setFishStruggleTimer(fishType.evasion);
                      }
                    }
                  }
                  else if (value == LineAction_Reel)
                  {
                    // If the fish is in the process of escaping, reeling causes it to escape
                    if (fishType.size != FishSize_None && castInfo[castIndex].escaping)
                    {
                      fishEscaped(&castInfo[castIndex]);
                    }
                    else
                    {
                      // Successful reel

                      // Tell the previous tile that they are now the end (or tell player the fish is caught)
                      enqueueCommOnFace(castInfo[castIndex].faceIn, Command_LineAction, LineAction_DoneReeling);

                      // Remove the cast from this tile
                      castInfo[castIndex].faceIn = INVALID_FACE;
                    }
                  }
                }
                else
                {
                  // Not the end of the cast - propagate the tug/reel down
                  enqueueCommOnFace(castInfo[castIndex].faceOut, command, value);
                }
              }
            }
            break;
#endif  // ENABLE_LAKE_TILE

          case LineAction_DoneReeling:
#ifdef ENABLE_LAKE_TILE
            // Line was reeled into this tile - line gets shortened by one
            for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
            {
              if (castInfo[castIndex].faceOut == f)
              {
                castInfo[castIndex].faceOut = INVALID_FACE;

                // Assuming the fish is not escaping
                setFishStruggleTimer(castInfo[castIndex].fishType.type.evasion);

                // Randomly put the fish into escape mode (mwuahaha)
                letFishStartEscaping(&castInfo[castIndex]);
              }
            }
#endif  // ENABLE_LAKE_TILE

#ifdef ENABLE_FISHERFOLK_TILE
            // Tentatively clear the player state
            // If the fish was actually caught, it will get overridden
            playerState = PlayerState_MustDisconnect;

            // If we have a fish on the hook then the fish was caught!
            if (playerCastInfo.faceOut == f && playerCastInfo.fishType.type.size != FishSize_None)
            {
              caughtFish();
            }
#endif  // ENABLE_FISHERFOLK_TILE
            break;
        }
      }
      break;

#ifdef ENABLE_LAKE_TILE
    case Command_LakeFX:
      {
        switch (value)
        {
          case LakeFX_ShoreWave:
            // Use the timer to prevent infinite spamming
            if (shoreWaveTimer.getRemaining() < 1000)
            {
              shoreWaveTimer.set(0);  // force expire now
            }
            break;

          case LakeFX_Lure:
            // Player tugged the line to lure nearby fish

            // TODO : Only work if there's a fish present
            
            tugFace = f;
            tugScare = !tugRepeatTimer.isExpired();
            tugRepeatTimer.set(TUG_REPEAT_RATE);
            // fall through to also do a ripple...
          case LakeFX_Ripple:
            rippleFace = f;
            rippleValue = 255;
            break;
        }
      }
      break;
#endif  // ENABLE_LAKE_TILE

    case Command_FishHooked:
      {
        // End of the line hooked a fish
        // This command just propagates the fish info back to the fisherfolk
#ifdef ENABLE_LAKE_TILE
        for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
        {
          if (castInfo[castIndex].faceIn != INVALID_FACE && castInfo[castIndex].faceOut == f)
          {
            castInfo[castIndex].fishType.rawBits = value;

            // If we're a lake tile, continue propagating it back up the line towards the player
            enqueueCommOnFace(castInfo[castIndex].faceIn, Command_FishHooked, value);
            break;
          }
        }
#endif  // ENABLE_LAKE_TILE

#ifdef ENABLE_FISHERFOLK_TILE
        playerCastInfo.fishType.rawBits = value;
#endif  // ENABLE_FISHERFOLK_TILE

      }
      break;
  }
}

// ----------------------------------------------------------------------------------------------------

#ifdef ENABLE_LAKE_TILE
void fishEscaped(CastInfo *castInfo)
{
  // Drop the fish back in the pond!
  for (byte fishIndex = 0; fishIndex < MAX_FISH_PER_TILE; fishIndex++)
  {
    // Find a slot for the fish - or else the fish gets discarded
    if (residentFish[fishIndex].fishType.type.size == FishSize_None)
    {
      residentFish[fishIndex].fishType = castInfo->fishType;
      residentFish[fishIndex].curFace = castInfo->faceIn;
      assignFishMoveTarget(fishIndex);
      residentFish[fishIndex].moveTimer.set(FISH_MOVE_RATE_PAUSE);
      break;
    }
  }
  // Remove the fish from the hook
  castInfo->fishType.type.size = FishSize_None;

  // Propagate the break up the line
  // Do this before calling breakLine() since it clears faceIn
  enqueueCommOnFace(castInfo->faceIn, Command_LineAction, LineAction_Break);
  breakLine(castInfo->faceIn);
}
#endif

// ----------------------------------------------------------------------------------------------------

void breakLine(byte sourceFace)
{
#ifdef ENABLE_FISHERFOLK_TILE
  // If a player receives this then their line was broken and they should go back to idle
  playerState = PlayerState_Idle;

  // If the break came from a different direction then make sure the line breaks too
  if (playerCastInfo.faceOut != INVALID_FACE && sourceFace != playerCastInfo.faceOut)
  {
    enqueueCommOnFace(playerCastInfo.faceOut, Command_LineAction, LineAction_Break);
  }

  playerCastInfo.faceOut = INVALID_FACE;
#endif  // ENABLE_FISHERFOLK_TILE

#ifdef ENABLE_LAKE_TILE
  // Use face to match the entry in our castInfo array
  for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
  {
    if (sourceFace == castInfo[castIndex].faceIn)
    {
      // Received the break from up the line - propagate it down if not already at the end
      if (castInfo[castIndex].faceOut != INVALID_FACE)
      {
        enqueueCommOnFace(castInfo[castIndex].faceOut, Command_LineAction, LineAction_Break);
      }
      castInfo[castIndex].faceIn = INVALID_FACE;
    }
    else if (castInfo[castIndex].faceOut != INVALID_FACE && sourceFace == castInfo[castIndex].faceOut)
    {
      // Received the break from down the line - propagate it up
      enqueueCommOnFace(castInfo[castIndex].faceIn, Command_LineAction, LineAction_Break);
      castInfo[castIndex].faceIn = INVALID_FACE;
    }
  }
#endif  // ENABLE_LAKE_TILE
}

// ----------------------------------------------------------------------------------------------------

#ifdef ENABLE_FISHERFOLK_TILE
void caughtFish()
{
  // Find a spot in our bag for the fish
  for (byte fishIndex = 0; fishIndex < MAX_CAUGHT_FISH; fishIndex++)
  {
    if (caughtFishInfo[fishIndex].display != FishDisplay_None)
    {
      continue;
    }

    // Save the fish in our bag!
    newlyCaughtFishIndex = fishIndex;
    FishType fishType = playerCastInfo.fishType.type;
    uint8_t displayIndex = randRange(0, 3) + (fishType.evasion - 1) * 2;
    caughtFishInfo[fishIndex].display = FishTypeToDisplayOptions[fishType.size - 1][displayIndex];
    // Random colors
    uint8_t primaryColor = randRange(0, 3);
    caughtFishInfo[fishIndex].color1 = RandomFishColor(primaryColor);
    primaryColor++;
    if (primaryColor > 2)
    {
      primaryColor = 0;
    }
    caughtFishInfo[fishIndex].color2 = RandomFishColor(primaryColor);

    playerState = PlayerState_FishCaught;
    playerAnimStage = 0;
    playerAnimStep = 0;
    playerAnimTimer.set(playerAnimStageDelays_FishCaught[playerAnimStage] << 4);
    break;
  }
}

uint16_t RandomFishColor(uint8_t primaryColor)
{
  // Random colors
  uint8_t color1 = 255;
  uint8_t subValue = randRange(64, 192);
  uint8_t color2 = color1 - subValue;
  subValue = randRange(64, 192);
  uint8_t color3 = (subValue >= color2) ? 0 : (color2 - subValue);

  uint8_t r, g, b;
  if (primaryColor == 0)
  {
    r = color1;
    color1 = color3;
  }
  else if (primaryColor == 1)
  {
    r = color2;
    color2 = color3;
  }
  else
  {
    r = color3;
  }

  uint8_t randGB = randRange(0, 2);
  if (randGB == 0)
  {
    g = color1;
    b = color2;
  }
  else
  {
    g = color2;
    b = color1;
  }

  return RGB_TO_U16(r, g, b);
}
#endif  // ENABLE_FISHERFOLK_TILE

// ----------------------------------------------------------------------------------------------------

#ifdef ENABLE_LAKE_TILE
void determineOurLakeDepth()
{
  // If we have an empty neighbor then we must be a shallow tile
  if (numNeighborLakeTiles < FACE_COUNT)
  {
    tileInfo.lakeDepth = LakeDepth_Shallow;
  }
  else
  {
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
}
#endif  // ENABLE_LAKE_TILE

// ====================================================================================================
// UTILITY
// ====================================================================================================

// Random code partially copied from blinklib because there's no function
// to set an explicit seed.
byte randGetByte()
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
byte randRange(byte min, byte max)
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
  commReceive();
  detectNeighbors();

#ifdef ENABLE_LAKE_TILE
  loop_Lake();
#endif  // ENABLE_LAKE_TILE

#ifdef ENABLE_FISHERFOLK_TILE
  loop_Player();
#endif // ENABLE_FISHERFOLK_TILE

  // If a command queue is empty then just send our tile info again
  FOREACH_FACE(f)
  {
    if (commInsertionIndexes[f] == 0)
    {
      enqueueCommOnFace(f, Command_TileInfo, tileInfo.tileType | (tileInfo.lakeDepth << 2));
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

#ifdef ENABLE_FISHERFOLK_TILE
      // Lake tile removed from next to the player - recompute casting info
      if (neighborTileInfo[f].tileType == TileType_Lake)
      {
        playerState = PlayerState_Idle;
        breakLine(f);
      }
#endif  // ENABLE_FISHERFOLK_TILE

#ifdef ENABLE_LAKE_TILE
      if (neighborTileInfo[f].tileType != TileType_NotPresent)
      {
        // Removing the source of a cast breaks the line
        for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
        {
          if (castInfo[castIndex].faceIn != INVALID_FACE)
          {
            if (castInfo[castIndex].faceIn == f || castInfo[castIndex].faceOut == f)
            {
              breakLine(f);
            }
          }
        }
      }
#endif

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
      }
      else if (neighborTileInfo[f].tileType == TileType_Lake)
      {
        numNeighborLakeTiles++;
      }
    }
  }

}

// ----------------------------------------------------------------------------------------------------

#ifdef ENABLE_LAKE_TILE
void loop_Lake()
{
  // Lake tiles recompute their depth based on received comm packets and neighbor updates
  determineOurLakeDepth();

  tryToSpawnFish();
  moveFish();
  fishStruggle();

  // Just for fun...
  if (buttonSingleClicked())
  {
    startRipple(false);
  }

  checkShoreWave();
  rippleOut();
}
#endif  // ENABLE_LAKE_TILE

// ----------------------------------------------------------------------------------------------------

#ifdef ENABLE_FISHERFOLK_TILE
void loop_Player()
{
  // Compute the number of fish already caught
  byte numFishCaught = 0;
  for (byte fishIndex = 0; fishIndex < MAX_CAUGHT_FISH; fishIndex++)
  {
    if (caughtFishInfo[fishIndex].display != FishDisplay_None)
    {
      numFishCaught++;
    }
  }

  switch (playerState)
  {
    case PlayerState_MustDisconnect:
      // Waiting for the player to disconnect other tiles
      if (numNeighborLakeTiles == 0)
      {
        playerState = PlayerState_Idle;
      }
      break;

    case PlayerState_Idle:
      {
        // Placing their tile next to the lake will make a player go into casting mode
        // It starts by sweeping back-and-forth between the lake tiles that it is touching
        if (numNeighborLakeTiles > 0)
        {
          // Reset the display in case the player was just browsing their bag
          fishDisplayingIndex = MAX_CAUGHT_FISH;

          // Can only cast when touching at most three lake tiles
          // These tiles must be contiguous - that check is further down
          if (numNeighborLakeTiles <= 3)
          {
            // Clear the buttonPressed flag
            buttonPressed();

            // Find any face with a lake neighbor
            FOREACH_FACE(f)
            {
              if (neighborTileInfo[f].tileType == TileType_Lake)
              {
                castFaceStart = castFaceEnd = f;
                break;
              }
            }

            // That tile may not be an edge
            // Find the starting lake tile by going CCW from there...
            byte consecutiveLakeFaces = 1;
            FOREACH_FACE(f)
            {
              byte faceTest = CCW_FROM_FACE(castFaceStart, 1);
              if (neighborTileInfo[faceTest].tileType != TileType_Lake)
              {
                break;
              }
              consecutiveLakeFaces++;
              castFaceStart = faceTest;
            }
            
            // ...and go CW from there to find the last face
            FOREACH_FACE(f)
            {
              byte faceTest = CW_FROM_FACE(castFaceEnd, 1);
              if (neighborTileInfo[faceTest].tileType != TileType_Lake)
              {
                break;
              }
              consecutiveLakeFaces++;
              castFaceEnd = faceTest;
            }

            if (consecutiveLakeFaces == numNeighborLakeTiles)
            {
              // Got the face range - start sweeping side to side
              playerState = PlayerState_GetCastAngle;
              castSweepDirection = CastSweepDirection_CW;
              castFace = castFaceStart;
              castAngle = 0;
            }
            else
            {
              // Non-contiguous lake tiles - force the player to disconnect
              playerState = PlayerState_MustDisconnect;
            }
          }
          else
          {
            // More than 3 lake tiles attached - force the player to disconnect
            playerState = PlayerState_MustDisconnect;
          }
        }
        else
        {
          // Player tile has no neighboring lake tiles
          // Let the user interact with it without casting
          if (buttonSingleClicked())
          {
            // Can only change our player color if we haven't caught any fish
            if (numFishCaught == 0)
            {
              playerColorIndex++;
              if (playerColorIndex >= MAX_PLAYER_COLORS)
              {
                playerColorIndex = 0;
              }
            }
            else
            {
              // Cycle through the player's captured fish with each click
              do
              {
                fishDisplayingIndex++;
                if (fishDisplayingIndex >= MAX_CAUGHT_FISH)
                {
                  fishDisplayingIndex = 0;
                }
              } while (caughtFishInfo[fishDisplayingIndex].display == FishDisplay_None);

              fishAnimFrame = fishDisplayAnimStartIndexes[caughtFishInfo[fishDisplayingIndex].display];
              fishAnimTimer.set(fishDisplayAnimFrames[fishAnimFrame].delay << 4);
            }
          }
          else if (buttonLongPressed())
          {
            // Toss the fish back (though it doesn't really get added back to the lake)
            playerState = PlayerState_ReleasingFish;
          }
        }
      }
      break;

    case PlayerState_ReleasingFish:
      caughtFishInfo[fishDisplayingIndex].display = FishDisplay_None;
      fishDisplayingIndex = MAX_CAUGHT_FISH;
      playerState = PlayerState_Idle;
      break;

    case PlayerState_GetCastAngle:
      {
        if (buttonPressed())
        {
          // Once the player clicks the button they lock in the angle and start selecting the power
          playerState = PlayerState_GetCastPower;
          castPower = 0;
          castSweepDirection = CastSweepDirection_Up;

          // Clear the released flag
          buttonReleased();
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
          // Releasing the button casts the line at the chosen angle+power
          playerState = PlayerState_WaitingForFish;

          // Find the angle (and thus pattern) based on the power
          byte power = castPower >> 6;
          byte dirIndex = 0;
          for (; dirIndex < MAX_CAST_ANGLES; dirIndex++)
          {
            if (powerToAngle[power][dirIndex] > castAngle)
            {
              break;
            }
          }

          byte castCommand = Command_CastDir0 + dirIndex;
          byte data = power;    // bits [3:2] are zero, indicating this is the first step
          enqueueCommOnFace(castFace, castCommand, data);

          playerCastInfo.faceOut = castFace;
          playerCastInfo.fishType.type.size = FishSize_None;

          // Now that the button is released, clear flags
          buttonSingleClicked();
          buttonLongPressed();
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

    case PlayerState_WaitingForFish:
      {
        // Clicking the player tile tugs the line to try to lure a fish
        if (buttonSingleClicked())
        {
          enqueueCommOnFace(castFace, Command_LineAction, LineAction_Tug);
        }
        
        // Long pressing reels in the line
        if (buttonLongPressed())
        {
          enqueueCommOnFace(castFace, Command_LineAction, LineAction_Reel);
        }
      }
      break;
  }
}
#endif  // ENABLE_FISHERFOLK_TILE

// ====================================================================================================
// FISH
// ====================================================================================================

#ifdef ENABLE_LAKE_TILE
void resetSpawnTimer()
{
  uint32_t spawnDelay = randRange(FISH_SPAWN_RATE_MIN, FISH_SPAWN_RATE_MAX) << 7;
  spawnTimer.set(spawnDelay + fishSpawnDelay);
}

// ----------------------------------------------------------------------------------------------------

void tryToSpawnFish()
{
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

  // Don't spawn if there's already a fish in this tile
  for (byte fishIndex = 0; fishIndex < MAX_FISH_PER_TILE; fishIndex++)
  {
    if (residentFish[fishIndex].fishType.type.size != FishSize_None)
    {
      return;
    }
  }

  // Spawn the fish in the first slot (guaranteed to be empty)
  byte fishIndex = 0;

  numFishToSpawn--;
  residentFish[fishIndex].fishType.type.size = (FishSize) tileInfo.lakeDepth;
  residentFish[fishIndex].fishType.type.evasion = randRange(1, 4);
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
  // 50/50 chance of leaving for a neighbor tile
  byte leaveTile = randRange(0, 2);

  // Figure out which neighbors are present, are lake tiles, and are of a depth that can accommodate this fish
  byte numValidTargetTiles = 0;
  FOREACH_FACE(f)
  {
    if (neighborTileInfo[f].tileType == TileType_Lake)
    {
      // Fish can swim to their own depth, one depth shallower, or one depth deeper
      char depthDiff = neighborTileInfo[f].lakeDepth - residentFish[fishIndex].fishType.type.size;
      if (depthDiff <= 1 && depthDiff >= -1)
      {
        fishMoveTargetTiles[numValidTargetTiles] = f;
        numValidTargetTiles++;

        // Luring a fish will either force it to pick the given tile, or force it to avoid it
        if (tugFace == f)
        {
          tugFace = INVALID_FACE;   // only lure one fish per tug
          if (tugScare)
          {
            numValidTargetTiles--;  // fish scared - skip it
          }
          else
          {
            // Fish will definitely go towards the tug
            fishMoveTargetTiles[0] = f;
            numValidTargetTiles = 1;
            leaveTile = 1;
            break;
          }
        }
      }
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
    FishType fishType = residentFish[fishIndex].fishType.type;
    if (fishType.size == FishSize_None)
    {
      // No fish in this slot
      continue;
    }

    if (!residentFish[fishIndex].moveTimer.isExpired())
    {
      // Fish in this slot is not ready to move yet
      continue;
    }
    residentFish[fishIndex].moveTimer.set(FISH_MOVE_RATE_NORMAL - 100 * fishType.evasion);

    // If the fish has reached its target then pick the next target
    if (residentFish[fishIndex].destNeighbor == INVALID_FACE &&
        residentFish[fishIndex].destFace == residentFish[fishIndex].curFace)
    {
      assignFishMoveTarget(fishIndex);

      // Pause a bit longer before continuing
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
      enqueueCommOnFace(residentFish[fishIndex].curFace, Command_FishTransfer, residentFish[fishIndex].fishType.rawBits);
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
    // Uses data space for the array instead of complex code computation. Code space is at more of a premium.
    byte nextFace = nextMoveFaceArray[destFaceInThisTile][residentFish[fishIndex].curFace];
    residentFish[fishIndex].curFace = nextFace;

    // If the fish is occupying the same face as the end of a cast line, there's a chance it will be hooked!
    for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
    {
      // Must be a valid cast that has not already hooked a fish
      if (castInfo[castIndex].faceIn != INVALID_FACE && castInfo[castIndex].fishType.type.size == FishSize_None)
      {
        // Can only catch at the end of the line (invalid faceOut)
        if (castInfo[castIndex].faceOut == INVALID_FACE)
        {
          if (castInfo[castIndex].faceIn == residentFish[fishIndex].curFace)
          {
            // Automatic hook...for now
            // TODO : Randomize (maybe? feels good as is)
            bool hooked = true;

            if (hooked)
            {
              // Save the fish info in the cast array
              FishType fishType = residentFish[fishIndex].fishType.type;
              castInfo[castIndex].fishType.type = fishType;
              setFishStruggleTimer(fishType.evasion);
              //castInfo[castIndex].debug = false;

              // Remove fish from the lake
              residentFish[fishIndex].fishType.type.size = FishSize_None;

              // Communicate the fish info back up the line.
              // By telling the player now, we don't need to track the fish from tile to tile as the
              // player reels it in.
              enqueueCommOnFace(castInfo[castIndex].faceIn, Command_FishHooked, (byte) castInfo[castIndex].fishType.rawBits);
            }
          }
        }
      }
    }
  }
}

void fishStruggle()
{
  for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
  {
    // Valid cast with a fish on the hook
    if (castInfo[castIndex].faceIn != INVALID_FACE &&
        castInfo[castIndex].faceOut == INVALID_FACE &&
        castInfo[castIndex].fishType.type.size != FishSize_None)
    {
      if (castInfo[castIndex].escapeTimer.isExpired())
      {
        if (castInfo[castIndex].escaping)
        {
          // Fish escaped :(
          fishEscaped(&castInfo[castIndex]);
        }
        else
        {
          // Fish may start struggling
          letFishStartEscaping(&castInfo[castIndex]);
        }
      }
    }
  }
}

void letFishStartEscaping(CastInfo *castInfo)
{
  byte escapeAttempt = randRange(0, 4);
  if (escapeAttempt < castInfo->fishType.type.evasion)
  {
    setFishStruggleTimer(castInfo->fishType.type.evasion);
    castInfo->escaping = true;
  }
}

void setFishStruggleTimer(uint8_t evasion)
{
  castInfo->escaping = false;
  castInfo->escapeTimer.set(FISH_STRUGGLE_DELAY - 1000 * evasion);
}

#endif // ENABLE_LAKE_TILE

// ====================================================================================================
// SHORE WAVE & RIPPLE
// ====================================================================================================

#ifdef ENABLE_LAKE_TILE

void checkShoreWave()
{
  // Fade the current wave - it should completely fade before the next wave starts
  if (shoreWaveValue >= SHORE_WAVE_FADE_RATE)
  {
    shoreWaveValue -= SHORE_WAVE_FADE_RATE;
  }
  else
  {
    shoreWaveValue = 0;
  }

  // Check if we should start the next wave
  if (!shoreWaveTimer.isExpired())
  {
    return;
  }

  // Tell neighbors that we just kicked off a wave
  FOREACH_FACE(f)
  {
    if (neighborTileInfo[f].tileType == TileType_Lake && neighborTileInfo[f].lakeDepth == LakeDepth_Shallow)
    {
      enqueueCommOnFace(f, Command_LakeFX, LakeFX_ShoreWave);
    }
  }
  shoreWaveTimer.set(SHORE_WAVE_RATE);
  shoreWaveValue = 255;
}

// ----------------------------------------------------------------------------------------------------

void startRipple(bool lure)
{
  rippleValue = 255;
  rippleFace = INVALID_FACE;

  FOREACH_FACE(f)
  {
    if (neighborTileInfo[f].tileType == TileType_Lake)
    {
      enqueueCommOnFace(f, Command_LakeFX, lure ? LakeFX_Lure : LakeFX_Ripple);
    }
  }
}

// ----------------------------------------------------------------------------------------------------

void rippleOut()
{
  if (rippleValue == 0)
  {
    return;
  }

  bool maySendOut = false;
  if (rippleFace == INVALID_FACE && rippleValue >= 192)
  {
    maySendOut = true;
  }

  if (rippleValue >= RIPPLE_FADE_RATE)
  {
    rippleValue -= RIPPLE_FADE_RATE;
  }
  else
  {
    rippleValue = 0;
  }

  if (maySendOut && rippleValue < 224)
  {
  }
}

#endif  // ENABLE_LAKE_TILE

// ====================================================================================================
// RENDER
// ====================================================================================================

void render()
{
  Color color;
  setColor(OFF);

#ifdef ENABLE_FISHERFOLK_TILE
  animatePlayer();
#endif

#ifdef ENABLE_LAKE_TILE
  // Base lake color
  FOREACH_FACE(f)
  {
    Color waterColor;
    waterColor.as_uint16 = WaterColors[tileInfo.lakeDepth];
    color = waterColor;

    if (tileInfo.lakeDepth == LakeDepth_Shallow)
    {
      byte shoreWaveColor = getWaveColor(neighborTileInfo[f].tileType == TileType_Lake, shoreWaveValue);
      lightenColor(&color, shoreWaveColor);
    }

    // Active ripple
    if (rippleValue > 0)
    {
      byte rippleColor = 0;
      if (rippleFace == INVALID_FACE)
      {
        rippleColor = getWaveColor(true, rippleValue);
      }
      else
      {
        byte sideFace1 = CW_FROM_FACE(f, 1);
        byte sideFace2 = CCW_FROM_FACE(f, 1);

        rippleColor = getWaveColor(rippleFace == f || rippleFace == sideFace1 || rippleFace == sideFace2, rippleValue);
        rippleColor >>= 1;
      }

      lightenColor(&color, rippleColor);
    }

    // Fish, if present
    for (byte fishIndex = 0; fishIndex < MAX_FISH_PER_TILE; fishIndex++)
    {
      if (residentFish[fishIndex].fishType.type.size != FishSize_None && residentFish[fishIndex].curFace == f)
      {
        // Fish presence darkens the water
        color.as_uint16 = (waterColor.as_uint16 >> 2) & 0b0011100111001110;

        /*
        color.b = color.r = color.g = 31;//>>= 1;

        // DEBUG SHOW FISH SIZE
        if (residentFish[fishIndex].fishType.type.size == FishSize_Medium)
        {
          color.b = 0;
        }
        else if (residentFish[fishIndex].fishType.type.size == FishSize_Large)
        {
          color.g = 0;
        }
        */
      }
    }

    setFaceColor(f, color);
  }

  // Cast line, if present
  for (byte castIndex = 0; castIndex < MAX_CASTS_PER_TILE; castIndex++)
  {
    if (castInfo[castIndex].faceIn != INVALID_FACE)
    {
      if (castInfo[castIndex].fishType.type.size != FishSize_None && castInfo[castIndex].faceOut == INVALID_FACE)
      {
        color.r = color.g = color.b = (millis() >> 5) & 0x1F;

        if (castInfo[castIndex].escaping)
        {
          color.g = color.b = 0;
        }

        setFaceColor(castInfo[castIndex].faceIn, color);
      }
      else
      {
        setFaceColor(castInfo[castIndex].faceIn, WHITE);
      }
      
      if (castInfo[castIndex].faceOut != INVALID_FACE)
      {
        setFaceColor(castInfo[castIndex].faceOut, WHITE);
      }
    }
  }
#endif // ENABLE_LAKE_TILE

#ifdef ENABLE_FISHERFOLK_TILE
  color.as_uint16 = playerColors[playerColorIndex];
  Color playerColorDim;
  playerColorDim.as_uint16 = (color.as_uint16 >> 1) & 0b0111101111011110;
  Color playerColorBrighter;
  playerColorBrighter.as_uint16 = (color.as_uint16 << 1) | color.as_uint16 | 0b0000100001000010;
  switch (playerState)
  {
    case PlayerState_MustDisconnect:
      setColor(playerColorDim);
      break;

    case PlayerState_Idle:
      if (fishDisplayingIndex == MAX_CAUGHT_FISH)
      {
        setColor(playerColorDim);
        if (playerAnimStage == 0 && playerAnimStep < FACE_COUNT)
        {
          setColorOnFace(color, playerAnimStep);
        }

        // In the idle state we show our bag and how many fish we have caught
        for (byte fishIndex = 0; fishIndex < MAX_CAUGHT_FISH; fishIndex++)
        {
          if (caughtFishInfo[fishIndex].display != FishDisplay_None)
          {
            setColorOnFace(playerColorBrighter, fishIndex);
          }
        }
      }
      else
      {
        animateFish();
      }
      break;

    case PlayerState_GetCastAngle:
    case PlayerState_WaitingForFish:
      {
        // The player's color goes on the opposite face
        byte otherFace = CW_FROM_FACE(castFace, 3);
        setFaceColor(otherFace, color);

        // Light up the face in the direction the player is casting
        // Give a bit of a blur with the sides to show the analog nature of the sweep
        if (castAngle < 128)
        {
          otherFace = CCW_FROM_FACE(castFace, 1);
          color = makeColorRGB(128, 128, 128);
          setFaceColor(otherFace, color);
          //color = makeColorRGB(128 + castAngle, 128 + castAngle, 128 + castAngle);
          //setFaceColor(castFace, color);
        }
        else
        {
          otherFace = CW_FROM_FACE(castFace, 1);
          color = makeColorRGB(128, 128, 128);
          setFaceColor(otherFace, color);
          //color = makeColorRGB(128 + (255 - castAngle), 128 + (255 - castAngle), 128 + (255 - castAngle));
        }
        setFaceColor(castFace, WHITE);

        if (millis() & 0x1)
        {
          //setFaceColor(castFaceStart, BLUE);
          //setFaceColor(castFaceEnd, YELLOW);
        }
      }
      break;

    case PlayerState_GetCastPower:
      {
        setFaceColor(castFace, WHITE);

        byte otherFace = CW_FROM_FACE(castFace, 3);
        setFaceColor(otherFace, color);

        if (castPower < 64)
        {
          byte otherFace = CW_FROM_FACE(castFace, 3);
          setFaceColor(otherFace, color);
        }
        else if (castPower < 128)
        {
          byte otherFace = CW_FROM_FACE(castFace, 2);
          setFaceColor(otherFace, color);
          otherFace = CCW_FROM_FACE(castFace, 2);
          setFaceColor(otherFace, color);
        }
        else if (castPower < 192)
        {
          byte otherFace = CW_FROM_FACE(castFace, 1);
          setFaceColor(otherFace, color);
          otherFace = CCW_FROM_FACE(castFace, 1);
          setFaceColor(otherFace, color);
        }
        else
        {
          setFaceColor(castFace, color);
        }
      }
      break;

    case PlayerState_FishCaught:
      {
        switch (playerAnimStage)
        {
          case 0:
            setFaceColor(playerAnimStep, color);
            break;
          case 1:
            FOREACH_FACE(f)
            {
              if (caughtFishInfo[f].display != FishDisplay_None && f < playerAnimStep)
              {
                setFaceColor(f, dim(color, CAUGHT_FISH_IN_BAG_DIM));
              }
            }
            setFaceColor(playerAnimStep, color);
            break;
          case 2:
            {
              FOREACH_FACE(f)
              {
                if (caughtFishInfo[f].display != FishDisplay_None)
                {
                  setFaceColor(f, dim(color, CAUGHT_FISH_IN_BAG_DIM));
                }
              }

              uint32_t timerVal = playerAnimTimer.getRemaining();
              uint32_t halfMaxTimer = playerAnimStageDelays_FishCaught[playerAnimStage] << 3;
              byte dimValue = 255;
              if (timerVal > halfMaxTimer)
              {
                dimValue = timerVal - halfMaxTimer; // (500-to-251) - 250 = 250-to-1
              }
              else
              {
                dimValue = halfMaxTimer - timerVal; // 250 - (250-to-0) = 0-to-250
              }
              Color dimmedColor = dim(color, dimValue);
              setFaceColor(newlyCaughtFishIndex, dimmedColor);
            }
            break;
        }
      }
      break;
  }
#endif  // ENABLE_FISHERFOLK_TILE

  FOREACH_FACE(f)
  {
    if (commInsertionIndexes[f] == COMM_INDEX_ERROR_OVERRUN)
    {
      setColorOnFace(MAGENTA, f);
    }
  }
}

#ifdef ENABLE_FISHERFOLK_TILE
void animatePlayer()
{
  if (!playerAnimTimer.isExpired())
  {
    return;
  }

  byte nextTimerVal = 0;

  switch (playerState)
  {
    case PlayerState_Idle:
    {
      nextTimerVal = PLAYER_ANIM_DELAY_IDLE1;

      switch (playerAnimStage)
      {
        case 0:
          playerAnimStep++;
          if (playerAnimStep >= FACE_COUNT)
          {
            playerAnimStage = 1;
            nextTimerVal = PLAYER_ANIM_DELAY_IDLE2;
          }
          break;

        default:
          playerAnimStage = 0;
          playerAnimStep = 0;
          break;
      }
    }
    break;

    case PlayerState_FishCaught:
    {
      // Fish caught animation
      // Stage 0: Player color cycles from face 0 to 5
      // Stage 1: Player color cycles from face 0 to the slot for the new fish
      // Stage 2: Pulse color in slot
      byte targetFace = 6;
      switch (playerAnimStage)
      {
        case 1:
          targetFace = newlyCaughtFishIndex;
        case 0:
          {
            playerAnimStep++;
            if (playerAnimStep >= targetFace)
            {
              playerAnimStep = 0;
              playerAnimStage++;
            }
          }
          break;

        case 2:
          // Stay in stage 2 until the player disconnects from the lake
          // Do nothing - just let the timer continue
          break;
      }

      // Start the new anim timer
      nextTimerVal = playerAnimStageDelays_FishCaught[playerAnimStage];
    }
    break;
  }
  // Start the new anim timer
  playerAnimTimer.set(nextTimerVal << 4);
}
#endif

#ifdef ENABLE_FISHERFOLK_TILE
void animateFish()
{
  if (fishDisplayingIndex >= MAX_CAUGHT_FISH)
  {
    setColor(WHITE);
    return;
  }

  if (fishAnimTimer.isExpired())
  {
    fishAnimFrame++;
    if (fishDisplayAnimFrames[fishAnimFrame].bitwiseColor1 == END_OF_ANIM)
    {
      fishAnimFrame = fishDisplayAnimStartIndexes[caughtFishInfo[fishDisplayingIndex].display];
    }
    fishAnimTimer.set(fishDisplayAnimFrames[fishAnimFrame].delay << 4);
  }

  byte fishFrameColor1 = fishDisplayAnimFrames[fishAnimFrame].bitwiseColor1;
  byte fishFrameColor2 = fishDisplayAnimFrames[fishAnimFrame].bitwiseColor2;
  Color color;
  FOREACH_FACE(f)
  {
    setColorOnFace(OFF, f);
    if (fishFrameColor1 & 1<<f)
    {
      color.as_uint16 = caughtFishInfo[fishDisplayingIndex].color1; 
      setColorOnFace(color, f);
    }
    else if (fishFrameColor2 & 1<<f)
    {
      color.as_uint16 = caughtFishInfo[fishDisplayingIndex].color2;
      setColorOnFace(color, f);
    }
  }
}
#endif

#ifdef ENABLE_LAKE_TILE
uint8_t getWaveColor(bool firstFace, uint8_t value)
{
  uint8_t color = 0;

  // Color progression as value goes from 255 -> 0
  //
  //         255             192             128             64              0
  //         +---------------+---------------+---------------+---------------+
  // Face 1: |    FADE IN    |           FADE OUT            |
  //         +---------------+---------------+---------------+---------------+
  // Face 2:                 |    FADE IN    |           FADE OUT            |
  //         +---------------+---------------+---------------+---------------+

  if (firstFace)
  {
    if (value >= 192)
    {
      color = (255 - value) >> 2;   // 0 - 63 :: 0 - 15
    }
    else if (value >= 64)
    {
      color = (value - 64) >> 3;    // 127 - 0 :: 15 - 0
    }
  }
  else
  {
    if (value < 192 && value >= 128)
    {
      color = (191 - value) >> 2;   // 0 - 63 :: 0 - 15
    }
    else if (value < 128)
    {
      color = value >> 3;           // 127 - 0 :: 15 - 0
    }
  }

  return color;
}

void lightenColor(Color *color, uint8_t val)
{
  color->r = addToColorComponent(color->r, val);
  color->g = addToColorComponent(color->g, val);
  color->b = addToColorComponent(color->b, val);
}

uint8_t addToColorComponent(uint8_t in, uint8_t val)
{
  uint8_t sum = in + val;

  // Color components in the pixelColor_t struct are only 5 bits
  if (sum < 32)
  {
    return sum;
  }

  return 31;
}
#endif // ENABLE_LAKE_TILE