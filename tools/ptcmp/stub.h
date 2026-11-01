// Заглушки вместо интерфейса pt2-clone: собираем его настоящий pt2_replayer.c без SDL и экрана
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define PAULA_VOICES 4
#define MOD_ROWS 64
#define MOD_SAMPLES 31
#define MAX_PATTERNS 256
#define MIN_BPM 32
#define MAX_BPM 255
#define SPECTRUM_BAR_NUM 23
#define ASSERT(x) ((void)0)
#define MAX_SAMPLE_LENGTH 0x1FFFE
#define CLAMP(x, lo, hi) (((x) < (lo)) ? (lo) : (((x) > (hi)) ? (hi) : (x)))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
enum { PLAY_MODE_NORMAL, PLAY_MODE_PATTERN };
enum { MODE_IDLE, MODE_EDIT, MODE_PLAY, MODE_RECORD };
enum { POINTER_MODE_IDLE, POINTER_MODE_EDIT, POINTER_MODE_PLAY, POINTER_MODE_RECORD };
enum { RECORD_PATT, RECORD_SONG };
enum { TEMPO_MODE_CIA, TEMPO_MODE_VBLANK };
#define INITIAL_DITHER_SEED 0

typedef struct note_t { uint8_t param, sample, command; uint16_t period; } note_t;
typedef struct moduleHeader_t { char name[21]; uint16_t patternTable[128], songLength; uint16_t initialTempo; } moduleHeader_t;
typedef struct moduleSample_t {
    volatile int8_t *volumeDisp; volatile int32_t *lengthDisp, *loopStartDisp, *loopLengthDisp;
    char text[23]; int8_t volume; uint8_t fineTune; int32_t offset, length, loopStart, loopLength;
} moduleSample_t;
typedef struct moduleChannel_t {
    int8_t *n_start, *n_wavestart, *n_loopstart, n_volume, n_dmabit;
    int8_t n_toneportdirec, n_pattpos, n_loopcount;
    uint8_t n_wavecontrol, n_glissfunk, n_sampleoffset, n_toneportspeed;
    uint8_t n_vibratocmd, n_tremolocmd, n_finetune, n_funkoffset, n_samplenum;
    uint8_t n_vibratopos, n_tremolopos;
    int16_t n_period, n_note, n_wantedperiod;
    uint16_t n_cmd, n_length, n_replen;
    uint32_t n_scopedelta, n_chanindex;
    uint8_t syncFlags; int8_t syncAnalyzerVolume, syncVuVolume; uint16_t syncAnalyzerPeriod;
} moduleChannel_t;
typedef struct module_t {
    bool loaded, modified; int8_t *sampleData;
    volatile int32_t tick, speed; int8_t row;
    moduleHeader_t header; moduleSample_t samples[MOD_SAMPLES]; moduleChannel_t channels[PAULA_VOICES];
    note_t *patterns[MAX_PATTERNS];
    int8_t currRow; int32_t currSpeed, currBPM; uint16_t currPos, currPattern;
    uint32_t rowsCounter, rowsInTotal;
} module_t;

// Всё, что реплеер трогает в интерфейсе: поля не значат ничего
typedef struct {
    module_t *mod;
    bool stepPlayEnabled, stepPlayBackwards, songPlaying, blockMarkFlag, pat2SmpOngoing, mod2WavOngoing, didQuantize, muted[PAULA_VOICES];
    bool metroFlag, swapChannelFlag, modLoaded, isWAVRendering, isSMPRendering, tuningToneFlag, rowVisitTable[128 * MOD_ROWS];
    int playMode, currMode, stepPlayLastMode, recordMode, timingMode, metroSpeed, metroChannel, hiLowInstr, editMoveAdd;
    int currSample, samplePos, sampleZero, f6Pos, f7Pos, f8Pos, f9Pos, f10Pos, initialTempo, initialSpeed, tuningChan;
    int8_t multiModeNext[4];
    uint8_t pat2SmpHQ; int pat2SmpPos, pat2SmpEndPos;
    uint16_t *currPatternDisp, *currPosEdPattDisp, *currPosDisp, *currEditPatternDisp, *currPlayPattDisp, *currLengthDisp, *currSongBPMDisp;
    volatile int8_t vuMeterVolumes[PAULA_VOICES], realVuMeterVolumes[PAULA_VOICES]; volatile uint8_t spectrumVolumes[SPECTRUM_BAR_NUM];
    uint32_t playbackSeconds; uint64_t playbackSecondsFrac;
    struct { bool a; } f;
    double dPerfFreq;
} editor_t;
extern editor_t editor;
typedef struct { int channel, pos, lastPos, mode; } cursor_t; extern cursor_t cursor;
typedef struct { bool updatePatternData, updateCurrPattText, updateSongPos, updateSongPattern, updateSongBPM, updateSongSize, updateStatusText, updatePosEd, posEdScreenShown, updateSongName, updateSongLength, updateCurrSampleName, updateCurrSampleVolume, updateCurrSampleLength, updateCurrSampleRepeat, updateCurrSampleRepLen, updateCurrSampleFineTune, updateCurrSampleNum, updateVolFromText, samplerScreenShown, editOpScreenShown; } ui_t; extern ui_t ui;
typedef struct { bool keepEditModeAfterStepPlay, enableE8xEffect, compoMode; int maxSampleLength; } config_t; extern config_t config;
#define CIA_PAL_CLK (3546895.0 / 5.0)
enum { DO_CARRY = 1, DONT_SET_ORDER = -1, DONT_SET_PATTERN = -1, UPDATE_VUMETER = 64, UPDATE_SPECTRUM_ANALYZER = 128 };
static const int8_t vuMeterHeights[65] = {0};
static const uint64_t tickDuration31fp[256] = {0};
typedef struct { bool ledFilterEnabled; bool locked; uint64_t tickSampleCounter, tickSampleCounterFrac, samplesPerTickInt, samplesPerTickFrac, samplesPerTickIntTab[256], samplesPerTickFracTab[256], tickTimeIntTab[256], tickTimeFracTab[256]; } audio_t; extern audio_t audio;
extern module_t *song;

extern const uint8_t vibratoTable[32];
extern const int16_t periodTable[(37 * 16) + 15];

void paulaWriteByte(uint32_t address, uint8_t data8);
void paulaWriteWord(uint32_t address, uint16_t data16);
void paulaWritePtr(uint32_t address, const int8_t *ptr);
int8_t *paulaGetNullSamplePtr(void);
bool tickReplayer(void);
void modSetTempo(int32_t bpm, bool doLockAudio);
void modPlay(int16_t patt, int16_t pos, int8_t row);
void initializeModuleChannels(module_t *m);
void doStopIt(bool resetPlayMode);
void turnOffVoices(void);
void setPattern(int16_t pattern);
void clearSong(void);
void clearSamples(void);
void modFree(void);
