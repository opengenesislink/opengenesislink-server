#include "opengenesis/scripting/script_engine.hpp"
#include "opengenesis/scripting/lsl_builtins.hpp"

#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace opengenesis::scripting {
namespace {

const char* kLslFunctions = R"LSL(
llAbs llAcos llAddToLandBanList llAddToLandPassList llAdjustDamage
llAdjustSoundVolume llAgentInExperience llAllowInventoryDrop llAngleBetween
llApplyImpulse llApplyRotationalImpulse llAsin llAtan2 llAttachToAvatar
llAttachToAvatarTemp llAvatarOnLinkSitTarget llAvatarOnSitTarget llAxes2Rot
llAxisAngle2Rot llBase64ToInteger llBase64ToString llBreakAllLinks llBreakLink
llCastRay llCeil llChar llClearCameraParams llClearLinkMedia llClearPrimMedia
llCloseRemoteDataChannel llCloud llCollisionFilter llCollisionSound llCollisionSprite
llComputeHash llCos llCreateCharacter llCreateKeyValue llCreateLink llCSV2List
llDamage llDataSizeKeyValue llDeleteCharacter llDeleteKeyValue llDeleteSubList
llDeleteSubString llDerezObject llDetachFromAvatar llDetectedDamage llDetectedGrab
llDetectedGroup llDetectedKey llDetectedLinkNumber llDetectedName llDetectedOwner
llDetectedPos llDetectedRezzer llDetectedRot llDetectedTouchBinormal
llDetectedTouchFace llDetectedTouchNormal llDetectedTouchPos llDetectedTouchST
llDetectedTouchUV llDetectedType llDetectedVel llDialog llDie llDumpList2String
llEdgeOfWorld llEjectFromLand llEmail llEscapeURL llEuler2Rot llEvade
llExecCharacterCmd llFabs llFindNotecardTextSync llFleeFrom llFloor
llForceMouselook llFrand llGenerateKey llGetAccel llGetAgentInfo llGetAgentLanguage
llGetAgentList llGetAgentSize llGetAlpha llGetAndResetTime llGetAnimation
llGetAnimationList llGetAnimationOverride llGetAttached llGetAttachedList
llGetAttachedListFiltered llGetBoundingBox llGetCameraAspect llGetCameraFOV
llGetCameraPos llGetCameraRot llGetCenterOfMass llGetClosestNavPoint llGetColor
llGetCreator llGetDate llGetDayLength llGetDayOffset llGetDisplayName llGetEnergy
llGetEnv llGetEnvironment llGetExperienceDetails llGetExperienceErrorMessage
llGetForce llGetFreeMemory llGetFreeURLs llGetGameControlMode
llGetGameControlModeAxes llGetGameControlModeButtons llGetGeometricCenter
llGetGMTclock llGetHealth llGetHTTPHeader llGetInventoryAcquireTime
llGetInventoryCreator llGetInventoryDesc llGetInventoryKey llGetInventoryName
llGetInventoryNumber llGetInventoryPermMask llGetInventoryType llGetKey
llGetLandOwnerAt llGetLinkKey llGetLinkMedia llGetLinkName llGetLinkNumber
llGetLinkNumberOfSides llGetLinkPrimitiveParams llGetLinkSitFlags
llGetListEntryType llGetListLength llGetLocalPos llGetLocalRot llGetMass
llGetMassMKS llGetMaxScaleFactor llGetMemoryLimit llGetMinScaleFactor
llGetMoonDirection llGetMoonRotation llGetNextEmail llGetNotecardLine
llGetNotecardLineSync llGetNumberOfNotecardLines llGetNumberOfPrims
llGetNumberOfSides llGetObjectAnimationNames llGetObjectDesc llGetObjectDetails
llGetObjectLinkKey llGetObjectMass llGetObjectName llGetObjectPermMask
llGetObjectPrimCount llGetOmega llGetOwner llGetOwnerKey llGetParcelDetails
llGetParcelFlags llGetParcelMaxPrims llGetParcelMusicURL llGetParcelPrimCount
llGetParcelPrimOwners llGetPermissions llGetPermissionsKey llGetPhysicsMaterial
llGetPos llGetPrimitiveParams llGetPrimMediaParams llGetRegionAgentCount
llGetRegionCorner llGetRegionDayLength llGetRegionDayOffset llGetRegionFlags
llGetRegionFPS llGetRegionMoonDirection llGetRegionMoonRotation llGetRegionName
llGetRegionSunDirection llGetRegionSunRotation llGetRegionTimeDilation
llGetRegionTimeOfDay llGetRenderMaterial llGetRootPosition llGetRootRotation
llGetRot llGetScale llGetScriptName llGetScriptState llGetSimStats
llGetSimulatorHostname llGetSPMaxMemory llGetStartParameter llGetStartString
llGetStaticPath llGetStatus llGetSubString llGetSunDirection llGetSunRotation
llGetTexture llGetTextureOffset llGetTextureRot llGetTextureScale llGetTime
llGetTimeOfDay llGetTimestamp llGetTorque llGetUnixTime llGetUsedMemory
llGetUsername llGetVel llGetVisualParams llGetWallclock llGiveAgentInventory
llGiveInventory llGiveInventoryList llGiveMoney llGodLikeRezObject llGround
llGroundContour llGroundNormal llGroundRepel llGroundSlope llHash llHMAC
llHTTPRequest llHTTPResponse llInsertString llInstantMessage llIntegerToBase64
llIsFriend llIsLinkGLTFMaterial llJson2List llJsonGetValue llJsonSetValue
llJsonValueType llKey2Name llKeyCountKeyValue llKeysKeyValue llLinear2sRGB
llLinkAdjustSoundVolume llLinkParticleSystem llLinkPlaySound
llLinksetDataAvailable llLinksetDataCountFound llLinksetDataCountKeys
llLinksetDataDelete llLinksetDataDeleteFound llLinksetDataDeleteProtected
llLinksetDataFindKeys llLinksetDataListKeys llLinksetDataRead
llLinksetDataReadProtected llLinksetDataReset llLinksetDataWrite
llLinksetDataWriteProtected llLinkSetSoundQueueing llLinkSetSoundRadius
llLinkSitTarget llLinkStopSound llList2CSV llList2Float llList2Integer
llList2Json llList2Key llList2List llList2ListSlice llList2ListStrided
llList2Rot llList2String llList2Vector llListen llListenControl llListenRemove
llListFindList llListFindListNext llListFindStrided llListInsertList
llListRandomize llListReplaceList llListSort llListSortStrided llListStatistics
llLoadURL llLog llLog10 llLookAt llLoopSound llLoopSoundMaster llLoopSoundSlave
llMakeExplosion llMakeFire llMakeFountain llMakeSmoke llManageEstateAccess
llMapBeacon llMapDestination llMD5String llMessageLinked llMinEventDelay
llModifyLand llModPow llMoveToTarget llName2Key llNavigateTo llOffsetTexture
llOpenFloater llOpenRemoteDataChannel llOrd llOverMyLand llOwnerSay
llParcelMediaCommandList llParcelMediaQuery llParseString2List
llParseStringKeepNulls llParticleSystem llPassCollisions llPassTouches
llPatrolPoints llPlaySound llPlaySoundSlave llPointAt llPow llPreloadSound
llPursue llPushObject llReadKeyValue llRefreshPrimURL llRegionSay llRegionSayTo
llReleaseCamera llReleaseControls llReleaseURL llRemoteDataReply
llRemoteDataSetRegion llRemoteLoadScript llRemoteLoadScriptPin
llRemoveFromLandBanList llRemoveFromLandPassList llRemoveInventory
llRemoveVehicleFlags llReplaceAgentEnvironment llReplaceEnvironment
llReplaceSubString llRequestAgentData llRequestDisplayName
llRequestExperiencePermissions llRequestInventoryData llRequestPermissions
llRequestSecureURL llRequestSimulatorData llRequestURL llRequestUserKey
llRequestUsername llResetAnimationOverride llResetLandBanList
llResetLandPassList llResetOtherScript llResetScript llResetTime
llReturnObjectsByID llReturnObjectsByOwner llRezAtRoot llRezObject
llRezObjectWithParams llRot2Angle llRot2Axis llRot2Euler llRot2Fwd llRot2Left
llRot2Up llRotateTexture llRotBetween llRotLookAt llRotTarget llRotTargetRemove
llRound llSameGroup llSay llScaleByFactor llScaleTexture llScriptDanger
llScriptProfiler llSendRemoteData llSensor llSensorRemove llSensorRepeat
llSetAgentEnvironment llSetAgentRot llSetAlpha llSetAngularVelocity
llSetAnimationOverride llSetBuoyancy llSetCameraAtOffset llSetCameraEyeOffset
llSetCameraParams llSetClickAction llSetColor llSetContentType llSetDamage
llSetEnvironment llSetForce llSetForceAndTorque llSetLinkGLTFOverrides
llSetGroundTexture llSetHoverHeight llSetInventoryPermMask llSetKeyframedMotion
llSetLinkAlpha llSetLinkCamera llSetLinkColor llSetLinkMedia
llSetLinkPrimitiveParams llSetLinkPrimitiveParamsFast llSetLinkRenderMaterial
llSetLinkSitFlags llSetLinkTexture llSetLinkTextureAnim llSetLocalRot
llSetMemoryLimit llSetObjectDesc llSetObjectName llSetObjectPermMask
llSetParcelMusicURL llSetPayPrice llSetPhysicsMaterial llSetPos
llSetPrimitiveParams llSetPrimMediaParams llSetPrimURL llSetRegionPos
llSetRemoteScriptAccessPin llSetRenderMaterial llSetRot llSetScale
llSetScriptState llSetSitText llSetSoundQueueing llSetSoundRadius llSetStatus
llSetText llSetTexture llSetTextureAnim llSetTimerEvent llSetTorque
llSetTouchText llSetVehicleFlags llSetVehicleFloatParam
llSetVehicleRotationParam llSetVehicleType llSetVehicleVectorParam
llSetVelocity llSHA1String llSHA256String llShout llSignRSA llSin llSitOnLink
llSitTarget llSleep llSound llSoundPreload llSqrt llsRGB2Linear
llStartAnimation llStartObjectAnimation llStopAnimation llStopObjectAnimation
llStopHover llStopLookAt llStopMoveToTarget llStopPointAt llStopSound
llStringLength llStringToBase64 llStringTrim llSubStringIndex llTakeCamera
llTakeControls llTan llTarget llTargetedEmail llTargetOmega llTargetRemove
llTeleportAgent llTeleportAgentGlobalCoords llTeleportAgentHome llTextBox
llToLower llToUpper llTransferLindenDollars llTransferOwnership llTriggerSound
llTriggerSoundLimited llUnescapeURL llUnSit llUpdateCharacter llUpdateKeyValue
llVecDist llVecMag llVecNorm llVerifyRSA llVolumeDetect llWanderWithin llWater
llWhisper llWind llWorldPosToHUD llXorBase64 llXorBase64Strings
llXorBase64StringsCorrect
)LSL";

ScriptFeatureStatus status_for(std::string_view name) {
    static const std::unordered_set<std::string> partial = {
        "llSay", "llWhisper", "llShout", "llOwnerSay", "llInstantMessage",
        "llSetTimerEvent", "llListen", "llSetPos", "llSetRegionPos",
        "llSetScale", "llSetVelocity", "llSetAngularVelocity", "llSetText",
        "llSetStatus", "llResetScript",
        "llApplyImpulse", "llApplyRotationalImpulse",
        "llSetForce", "llSetTorque", "llSetBuoyancy",
        // Executable pure builtins with deliberately conservative fidelity
        // status because edge behavior/Unicode still differs from SL LSL.
        "llChar", "llOrd", "llDeleteSubString", "llGetSubString",
        "llInsertString", "llStringTrim", "llSqrt", "llLog", "llLog10",
        "llGetTimeOfDay", "llReplaceSubString", "llListSort",
        "llListRandomize", "llList2ListStrided"
    };
    static const std::unordered_set<std::string> unsupported = {
        "llCloseRemoteDataChannel", "llCloud", "llGodLikeRezObject",
        "llMakeExplosion", "llMakeFire", "llMakeFountain", "llMakeSmoke",
        "llOpenFloater", "llOpenRemoteDataChannel", "llPointAt",
        "llRefreshPrimURL", "llReleaseCamera", "llRemoteDataReply",
        "llRemoteDataSetRegion", "llRemoteLoadScript", "llSendRemoteData",
        "llSetInventoryPermMask", "llSetObjectPermMask", "llSetPrimURL",
        "llSound", "llSoundPreload", "llStopPointAt", "llTakeCamera",
        "llXorBase64Strings", "llXorBase64StringsCorrect"
    };
    if (partial.contains(std::string{name})) {
        return ScriptFeatureStatus::partial;
    }
    if (lsl_builtin_implemented(name)) {
        return ScriptFeatureStatus::implemented;
    }
    if (unsupported.contains(std::string{name})) {
        return ScriptFeatureStatus::unsupported;
    }
    return ScriptFeatureStatus::recognized;
}

} // namespace

const std::vector<ScriptFeature>& lsl_function_catalog() {
    static const std::vector<ScriptFeature> functions = [] {
        std::vector<ScriptFeature> result;
        std::istringstream input(kLslFunctions);
        std::string name;
        while (input >> name) {
            result.push_back({
                .name = name,
                .category = "function",
                .status = status_for(name)});
        }
        return result;
    }();
    return functions;
}

} // namespace opengenesis::scripting
