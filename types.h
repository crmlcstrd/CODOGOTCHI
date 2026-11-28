#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

// --- Pet State ---
struct PetState {                     
  volatile uint8_t  mood = 1;         
  volatile uint8_t  health = 60;      
  volatile uint8_t  level = 0;        // 0..50 (website starts at level 0)
  volatile uint8_t  eyes = 0;         // equipped eyes accessory id (cosmeticId), 0 = none
  volatile uint8_t  mouth = 0;        // equipped mouth accessory id (cosmeticId), 0 = none
  volatile bool     cosmeticChanged = false;
  volatile uint32_t celebrateSeq = 0;
  volatile bool     sleepNow = false;
  volatile bool     syncRequested = false;
  volatile bool     celebratePending = false;
  volatile uint32_t petSeq = 0;          // website "Pet" counter (not persisted: first fetch after boot just sets the baseline)
  volatile bool     petSeqKnown = false;
  volatile bool     petPending = false;
  volatile bool     levelUpPending = false; // Renamed from ageUpPending
  volatile uint32_t lastSyncOkMs = 0;
  volatile uint32_t demoUntil = 0;    
};

// --- Sound ---
struct Note { uint16_t f0, f1, ms; };

enum Snd : uint8_t { 
  SND_IDLE, SND_HAPPY, SND_SAD, SND_CELEBRATE, SND_POKE, SND_PURR,
  SND_ANNOYED, SND_SYNC, SND_WAKE, SND_MUTE_ON, SND_MUTE_OFF, SND_COUNT 
};

struct Pattern { const Note* n; uint8_t len; };

// --- Touch ---
struct Seg { uint8_t pin; float base; uint32_t last; bool on; };

enum Gesture : uint8_t { 
  G_NONE, G_TAP1, G_TAP2, G_TAP3, G_SPAM, G_PET_START, G_PET_END,
  G_SWIPE_L, G_SWIPE_R, G_SYNC_HOLD 
};

// --- Face Model ---
struct Face {
  float eyeOpen;    
  float droop;      
  float smile;      
  float mouth;      
  float lookX, lookY;  
  float blush;      
  float happyEyes;  
};

enum Rx : uint8_t { 
  RX_NONE, RX_CELEBRATE, RX_POKE, RX_PET, RX_ANNOYED, RX_STATUS, RX_SYNC 
};

#endif