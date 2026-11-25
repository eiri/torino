#define AMP_PARAM_0 DB("volume", 32, -60, 0)
#define AMP_PARAM_1 NUM("gain", 33)
#define AMP_PARAM_2 NUM("gain2", 34)
#define AMP_PARAM_3 NUM("master", 35)
#define AMP_PARAM_4 NUM("treb", 36)
#define AMP_PARAM_5 NUM("mid", 37)
#define AMP_PARAM_6 NUM("bass", 38)
#define AMP_PARAM_7 NUM("presence", 39)
#define AMP_PARAM_8 NUM("bias", 42)
#define AMP_PARAM_9 ENUM("gatePreset", 47, "off|low|mid|high|super|custom")
#define AMP_PARAM_10 ENUM("cabsimType", 49, "none|57dlx|59bman|65dlx|65prince|57champ|4x12m|2x12c|4x12g|65twn|4x12v|2x12ss|1x12ss")
#define AMP_PARAM_11 ENUM("sag", 51, "less|match|more")
#define AMP_PARAM_12 BOOL("bright", 52)
static const param_t amp_241_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_9, AMP_PARAM_10};
static const param_t amp_124_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_103_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_246_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_7, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_100_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_7, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_106_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_83_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_117_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11, AMP_PARAM_12};
static const param_t amp_249_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_255_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_3, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_7, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_97_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_3, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11, AMP_PARAM_12};
static const param_t amp_121_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_7, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_94_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_3, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_7, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_252_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_3, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_114_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_2, AMP_PARAM_3, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_93_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_3, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_7, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};
static const param_t amp_109_params[] = {AMP_PARAM_0, AMP_PARAM_1, AMP_PARAM_3, AMP_PARAM_4, AMP_PARAM_5, AMP_PARAM_6, AMP_PARAM_7, AMP_PARAM_8, AMP_PARAM_9, AMP_PARAM_10, AMP_PARAM_11};

static const model_t models[] = {
    {"DUBS_LinearGain", 0, 241, 13, 129, {0}, {255, 129, 129, 129, 129, 129, 129, 129, 0, 129, 129, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}, amp_241_params, sizeof(amp_241_params) / sizeof(param_t), 0x1f},
    {"DUBS_Champ57", 0, 124, 12, 128, {0}, {172, 180, 1, 255, 129, 129, 129, 129, 0, 129, 129, 0, 0, 0, 0, 1, 0, 5, 0, 1, 0, 0, 0}, amp_124_params, sizeof(amp_124_params) / sizeof(param_t), 0x1f},
    {"DUBS_Deluxe57", 0, 103, 1, 128, {0}, {172, 155, 129, 129, 192, 129, 129, 129, 0, 129, 129, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0}, amp_103_params, sizeof(amp_103_params) / sizeof(param_t), 0x1f},
    {"DUBS_Twin57", 0, 246, 14, 129, {0}, {172, 78, 129, 129, 172, 123, 157, 189, 0, 129, 129, 0, 0, 0, 0, 0, 0, 9, 0, 1, 0, 0, 0}, amp_246_params, sizeof(amp_246_params) / sizeof(param_t), 0x1f},
    {"DUBS_Bassman59", 0, 100, 2, 128, {0}, {172, 163, 129, 129, 129, 123, 163, 146, 0, 129, 129, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 0, 0}, amp_100_params, sizeof(amp_100_params) / sizeof(param_t), 0x1f},
    {"DUBS_Princeton65", 0, 106, 4, 128, {0}, {172, 86, 1, 255, 155, 204, 76, 129, 0, 129, 129, 0, 0, 0, 0, 0, 0, 4, 0, 1, 0, 0, 0}, amp_106_params, sizeof(amp_106_params) / sizeof(param_t), 0x1f},
    {"DUBS_Deluxe65", 0, 83, 3, 0, {0}, {172, 115, 1, 255, 146, 209, 58, 1, 0, 1, 129, 0, 0, 0, 0, 0, 0, 3, 0, 1, 0, 0, 0}, amp_83_params, sizeof(amp_83_params) / sizeof(param_t), 0x1f},
    {"DUBS_Twin65", 0, 117, 5, 128, {0}, {172, 86, 129, 100, 180, 189, 172, 129, 0, 129, 129, 0, 0, 0, 0, 0, 0, 9, 0, 1, 0, 0, 0}, amp_117_params, sizeof(amp_117_params) / sizeof(param_t), 0x1f},
    {"DUBS_Silvertone", 0, 249, 15, 129, {0}, {172, 129, 129, 129, 172, 129, 157, 129, 0, 129, 129, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0}, amp_249_params, sizeof(amp_249_params) / sizeof(param_t), 0x1f},
    {"DUBS_DR103", 0, 255, 17, 129, {0}, {186, 157, 129, 255, 186, 152, 166, 135, 0, 129, 129, 0, 0, 0, 0, 0, 0, 10, 0, 1, 0, 0, 0}, amp_255_params, sizeof(amp_255_params) / sizeof(param_t), 0x3b},
    {"DUBS_Ac30Tb", 0, 97, 7, 128, {0}, {172, 163, 129, 100, 155, 129, 177, 1, 0, 129, 129, 0, 0, 0, 0, 0, 0, 7, 0, 1, 0, 0, 0}, amp_97_params, sizeof(amp_97_params) / sizeof(param_t), 0x3b},
    {"DUBS_Plexi87", 0, 121, 11, 128, {0}, {172, 255, 129, 126, 172, 92, 197, 129, 0, 129, 129, 0, 0, 0, 0, 1, 0, 8, 0, 1, 0, 0, 0}, amp_121_params, sizeof(amp_121_params) / sizeof(param_t), 0x1f},
    {"DUBS_Jcm800", 0, 94, 9, 128, {0}, {172, 255, 129, 126, 172, 92, 197, 129, 0, 129, 129, 0, 0, 0, 0, 1, 0, 6, 0, 1, 0, 0, 0}, amp_94_params, sizeof(amp_94_params) / sizeof(param_t), 0x3b},
    {"DUBS_Or120", 0, 252, 16, 129, {0}, {172, 140, 129, 129, 157, 100, 129, 129, 0, 129, 129, 0, 0, 0, 0, 1, 0, 8, 0, 0, 0, 0, 0}, amp_252_params, sizeof(amp_252_params) / sizeof(param_t), 0x3b},
    {"DUBS_SuperSonic", 0, 114, 6, 128, {0}, {172, 189, 132, 86, 155, 163, 155, 129, 0, 129, 129, 0, 0, 0, 0, 2, 0, 12, 0, 1, 0, 0, 0}, amp_114_params, sizeof(amp_114_params) / sizeof(param_t), 0x73},
    {"DUBS_Rect2", 0, 93, 10, 128, {0}, {172, 143, 129, 103, 166, 27, 200, 115, 0, 129, 129, 0, 0, 0, 0, 3, 0, 10, 0, 1, 0, 0, 0}, amp_93_params, sizeof(amp_93_params) / sizeof(param_t), 0x3b},
    {"DUBS_Evh3", 0, 109, 8, 128, {0}, {172, 166, 129, 86, 155, 78, 146, 143, 0, 129, 129, 0, 0, 0, 0, 2, 0, 8, 0, 1, 0, 0, 0}, amp_109_params, sizeof(amp_109_params) / sizeof(param_t), 0x3b},
    {"DUBS_Overdrive", 1, 60, 0, 0, {0, 8, 1}, {129, 129, 129, 129, 129, 0}, drive_params, sizeof(drive_params) / sizeof(param_t), 0x1f},
    {"DUBS_Greenbox", 1, 186, 0, 0, {0, 8, 1}, {129, 177, 140, 255, 0, 0}, green_params, sizeof(green_params) / sizeof(param_t), 0xf},
    {"DUBS_Blackbox", 1, 273, 0, 0, {0, 8, 1}, {129, 129, 86, 0, 0, 0}, black_params, sizeof(black_params) / sizeof(param_t), 0x7},
    {"DUBS_BigFuzz", 1, 271, 0, 0, {0, 8, 1}, {172, 172, 115, 0, 0, 0}, fuzz_params, sizeof(fuzz_params) / sizeof(param_t), 0x7},
    {"DUBS_VariFuzz", 1, 26, 0, 0, {0, 8, 1}, {72, 129, 0, 128, 128, 0}, vari_params, sizeof(vari_params) / sizeof(param_t), 0x7},
    {"DUBS_SimpleCompressor", 1, 136, 0, 0, {8, 8, 1}, {1, 0, 0, 0, 0, 0}, comp_params, sizeof(comp_params) / sizeof(param_t), 0x1},
    {"DUBS_Sustain", 1, 7, 0, 0, {0, 8, 1}, {128, 64, 1, 13, 128, 0}, sustain_params, sizeof(sustain_params) / sizeof(param_t), 0x3},
    {"DUBS_ChorusTriangle", 2, 19, 0, 0, {1, 1, 1}, {95, 15, 27, 27, 129, 0}, chorus_params, sizeof(chorus_params) / sizeof(param_t), 0x7},
    {"DUBS_TriangleFlanger", 2, 25, 0, 0, {1, 1, 1}, {255, 1, 255, 52, 66, 0}, flanger_params, sizeof(flanger_params) / sizeof(param_t), 0xf},
    {"DUBS_Vibratone", 2, 45, 0, 0, {1, 1, 1}, {246, 255, 41, 174, 132, 0}, rotor_params, sizeof(rotor_params) / sizeof(param_t), 0x1f},
    {"DUBS_SineTremolo", 2, 65, 0, 0, {1, 1, 1}, {220, 155, 126, 1, 1, 0}, trem_params, sizeof(trem_params) / sizeof(param_t), 0x3},
    {"DUBS_Phaser", 2, 79, 0, 0, {1, 1, 1}, {254, 1, 254, 186, 0, 0}, phaser_params, sizeof(phaser_params) / sizeof(param_t), 0x7},
    {"DUBS_StepFilter", 2, 41, 0, 0, {1, 1, 1}, {255, 129, 129, 129, 129, 0}, filter_params, sizeof(filter_params) / sizeof(param_t), 0x1f},
    {"DUBS_EcFilter", 2, 245, 0, 0, {1, 8, 1}, {237, 129, 7, 255, 0, 0}, wah_params, sizeof(wah_params) / sizeof(param_t), 0x1},
    {"DUBS_MonoDelay", 3, 22, 0, 0, {2, 1, 1}, {255, 129, 129, 129, 129, 0}, delay_params, sizeof(delay_params) / sizeof(param_t), 0xf},
    {"DUBS_TapeDelayLite", 3, 43, 0, 0, {2, 1, 1}, {126, 29, 1, 100, 129, 1}, tape_params, sizeof(tape_params) / sizeof(param_t), 0xf},
    {"DUBS_ReverseDelay", 3, 70, 0, 0, {2, 1, 1}, {255, 129, 129, 129, 129, 0}, reverse_params, sizeof(reverse_params) / sizeof(param_t), 0x3},
    {"DUBS_LargeHallReverb", 4, 58, 0, 0, {0, 8, 1}, {81, 64, 129, 7, 177, 0}, reverb_params, sizeof(reverb_params) / sizeof(param_t), 0x13},
    {"DUBS_SmallRoomReverb", 4, 38, 0, 0, {0, 8, 1}, {129, 129, 129, 129, 129, 0}, reverb_params, sizeof(reverb_params) / sizeof(param_t), 0x13},
    {"DUBS_Spring65", 4, 11, 0, 0, {0, 8, 1}, {129, 140, 75, 255, 129, 0}, spring_params, sizeof(spring_params) / sizeof(param_t), 0x13},
    {"DUBS_LargePlate", 4, 75, 0, 0, {0, 8, 1}, {58, 129, 146, 129, 183, 0}, reverb_params, sizeof(reverb_params) / sizeof(param_t), 0x13},
    {"DUBS_ArenaReverb", 4, 77, 0, 0, {0, 8, 1}, {255, 129, 129, 129, 129, 0}, reverb_params, sizeof(reverb_params) / sizeof(param_t), 0x13},
};
