#pragma once

typedef enum {
    SaflokProtocolFeatureNone = 0,
    SaflokProtocolFeatureEmulateUid = 1UL << 0,
    SaflokProtocolFeatureEmulateFull = 1UL << 1,
    SaflokProtocolFeatureEditUid = 1UL << 2,
    SaflokProtocolFeatureMoreInfo = 1UL << 3,
} SaflokProtocolFeature;

typedef enum {
    SaflokProtocolSupportSceneInfo,
    SaflokProtocolSupportSceneMoreInfo,
    SaflokProtocolSupportSceneRead,
    SaflokProtocolSupportSceneReadMenu,
    SaflokProtocolSupportSceneReadSuccess,
    SaflokProtocolSupportSceneSavedMenu,
    SaflokProtocolSupportSceneSaveName,
    SaflokProtocolSupportSceneEmulate,
    SaflokProtocolSupportSceneRpc,

    SaflokProtocolSupportSceneCount,
} SaflokProtocolSupportScene;
