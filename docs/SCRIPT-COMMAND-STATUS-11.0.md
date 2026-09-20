# OpenGenesisLINK 11.0 — Script Command Status

Diese Datei wird aus den in 11.0 gepflegten ScriptEngine-Katalogen abgeleitet. Sie ist die Wiki-Referenz für den Implementierungsstand der nativen OGL-Skriptsprache und der LSL-Kompatibilität.

## Statusdefinition

| Status | Bedeutung |
|---|---|
| ✅ `implemented` | Im 9.0-Vertrag ausführbar und für den dokumentierten Umfang implementiert. |
| 🟡 `partial` | Ausführbar, aber Semantik/Fidelity ist gegenüber vollständigem LSL bewusst eingeschränkt. |
| 🔵 `recognized` | Katalog-/Parserwissen vorhanden, aber die eigentliche Server-Semantik ist noch nicht ausführbar. |
| ⛔ `unsupported` | Derzeit absichtlich nicht unterstützt, veraltet oder nicht sicher/geeignet für den aktuellen Engine-Vertrag. |

**Wichtig:** Katalogabdeckung ist nicht dasselbe wie semantische Kompatibilität.

## Zusammenfassung

| Bereich | Gesamt | Implementiert | Partial | Recognized | Unsupported | Strikt implementiert | Ausführbar inkl. Partial | Katalogisiert |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| OGL | 31 | 31 | 0 | 0 | 0 | 100.00% | 100.00% | 100.00% |
| LSL Funktionen | 523 | 56 | 24 | 418 | 25 | 10.71% | 15.30% | 100.00% |
| LSL Events | 44 | 1 | 3 | 39 | 1 | 2.27% | 9.09% | 100.00% |

### Metrik

- **Strikt implementiert** = `implemented / total × 100`.
- **Ausführbar inkl. Partial** = `(implemented + partial) / total × 100`.
- **Katalogisiert** = alle Einträge, unabhängig davon ob sie ausführbar sind.
- Es wird **kein gewichteter Marketing-Prozentsatz** verwendet.

## OGL-Befehle und Sprachmerkmale

| Befehl | Kategorie | Status |
|---|---|---|
| `state` | language | ✅ implemented |
| `on` | language | ✅ implemented |
| `let` | language | ✅ implemented |
| `inc` | language | ✅ implemented |
| `goto` | language | ✅ implemented |
| `emit` | runtime | ✅ implemented |
| `timer.every` | runtime | ✅ implemented |
| `chat.listen` | runtime | ✅ implemented |
| `owner.notify` | social | ✅ implemented |
| `user.message` | social | ✅ implemented |
| `world.move` | world | ✅ implemented |
| `world.rotate` | world | ✅ implemented |
| `world.scale` | world | ✅ implemented |
| `world.velocity` | world | ✅ implemented |
| `world.angular_velocity` | world | ✅ implemented |
| `world.physics` | world | ✅ implemented |
| `world.text` | world | ✅ implemented |
| `world.say` | world | ✅ implemented |
| `world.whisper` | world | ✅ implemented |
| `world.shout` | world | ✅ implemented |
| `world.object` | query | ✅ implemented |
| `world.region` | query | ✅ implemented |
| `world.terrain` | query | ✅ implemented |
| `world.water` | query | ✅ implemented |
| `world.time` | query | ✅ implemented |
| `world.nearby` | query | ✅ implemented |
| `stop` | runtime | ✅ implemented |
| `if/else` | language | ✅ implemented |
| `while/for` | language | ✅ implemented |
| `functions` | language | ✅ implemented |
| `typed values` | language | ✅ implemented |


## LSL Events

| Befehl | Kategorie | Status |
|---|---|---|
| `attach` | event | 🔵 recognized |
| `at_rot_target` | event | 🔵 recognized |
| `at_target` | event | 🔵 recognized |
| `changed` | event | 🔵 recognized |
| `collision` | event | 🔵 recognized |
| `collision_end` | event | 🔵 recognized |
| `collision_start` | event | 🔵 recognized |
| `control` | event | 🔵 recognized |
| `dataserver` | event | 🔵 recognized |
| `email` | event | 🔵 recognized |
| `event_order` | documentation | ⛔ unsupported |
| `experience_permissions` | event | 🔵 recognized |
| `experience_permissions_denied` | event | 🔵 recognized |
| `final_damage` | event | 🔵 recognized |
| `game_control` | event | 🔵 recognized |
| `http_request` | event | 🔵 recognized |
| `http_response` | event | 🔵 recognized |
| `land_collision` | event | 🔵 recognized |
| `land_collision_end` | event | 🔵 recognized |
| `land_collision_start` | event | 🔵 recognized |
| `linkset_data` | event | 🔵 recognized |
| `link_message` | event | 🔵 recognized |
| `listen` | event | 🟡 partial |
| `money` | event | 🔵 recognized |
| `moving_end` | event | 🔵 recognized |
| `moving_start` | event | 🔵 recognized |
| `not_at_rot_target` | event | 🔵 recognized |
| `not_at_target` | event | 🔵 recognized |
| `no_sensor` | event | 🔵 recognized |
| `object_rez` | event | 🔵 recognized |
| `on_damage` | event | 🔵 recognized |
| `on_death` | event | 🔵 recognized |
| `on_rez` | event | 🔵 recognized |
| `path_update` | event | 🔵 recognized |
| `remote_data` | event | 🔵 recognized |
| `run_time_permissions` | event | 🔵 recognized |
| `sensor` | event | 🔵 recognized |
| `state_entry` | event | 🟡 partial |
| `state_exit` | event | 🔵 recognized |
| `timer` | event | ✅ implemented |
| `touch` | event | 🔵 recognized |
| `touch_end` | event | 🔵 recognized |
| `touch_start` | event | 🟡 partial |
| `transaction_result` | event | 🔵 recognized |


## LSL Funktionen — vollständiger OpenGenesisLINK-Katalog

Der OpenGenesisLINK-Katalog enthält 523 normalisierte kanonische `ll*`-Identifier. Die weiterhin als Referenz verwendete offizielle Second-Life-Wiki-Kategorie meldet 534 Seiten. Diese beiden Zahlen werden nicht gleichgesetzt; die Differenz wird nicht als zusätzliche implementierte Funktion interpretiert.

Referenz:
- https://wiki.secondlife.com/wiki/Category:LSL_Functions
- https://wiki.secondlife.com/wiki/Category:LSL_Events

| Befehl | Status |
|---|---|
| `llAbs` | ✅ implemented |
| `llAcos` | ✅ implemented |
| `llAddToLandBanList` | 🔵 recognized |
| `llAddToLandPassList` | 🔵 recognized |
| `llAdjustDamage` | 🔵 recognized |
| `llAdjustSoundVolume` | 🔵 recognized |
| `llAgentInExperience` | 🔵 recognized |
| `llAllowInventoryDrop` | 🔵 recognized |
| `llAngleBetween` | ✅ implemented |
| `llApplyImpulse` | 🔵 recognized |
| `llApplyRotationalImpulse` | 🔵 recognized |
| `llAsin` | ✅ implemented |
| `llAtan2` | ✅ implemented |
| `llAttachToAvatar` | 🔵 recognized |
| `llAttachToAvatarTemp` | 🔵 recognized |
| `llAvatarOnLinkSitTarget` | 🔵 recognized |
| `llAvatarOnSitTarget` | 🔵 recognized |
| `llAxes2Rot` | 🔵 recognized |
| `llAxisAngle2Rot` | ✅ implemented |
| `llBase64ToInteger` | ✅ implemented |
| `llBase64ToString` | ✅ implemented |
| `llBreakAllLinks` | 🔵 recognized |
| `llBreakLink` | 🔵 recognized |
| `llCastRay` | 🔵 recognized |
| `llCeil` | ✅ implemented |
| `llChar` | 🟡 partial |
| `llClearCameraParams` | 🔵 recognized |
| `llClearLinkMedia` | 🔵 recognized |
| `llClearPrimMedia` | 🔵 recognized |
| `llCloseRemoteDataChannel` | ⛔ unsupported |
| `llCloud` | ⛔ unsupported |
| `llCollisionFilter` | 🔵 recognized |
| `llCollisionSound` | 🔵 recognized |
| `llCollisionSprite` | 🔵 recognized |
| `llComputeHash` | 🔵 recognized |
| `llCos` | ✅ implemented |
| `llCreateCharacter` | 🔵 recognized |
| `llCreateKeyValue` | 🔵 recognized |
| `llCreateLink` | 🔵 recognized |
| `llCSV2List` | ✅ implemented |
| `llDamage` | 🔵 recognized |
| `llDataSizeKeyValue` | 🔵 recognized |
| `llDeleteCharacter` | 🔵 recognized |
| `llDeleteKeyValue` | 🔵 recognized |
| `llDeleteSubList` | ✅ implemented |
| `llDeleteSubString` | 🟡 partial |
| `llDerezObject` | 🔵 recognized |
| `llDetachFromAvatar` | 🔵 recognized |
| `llDetectedDamage` | 🔵 recognized |
| `llDetectedGrab` | 🔵 recognized |
| `llDetectedGroup` | 🔵 recognized |
| `llDetectedKey` | 🔵 recognized |
| `llDetectedLinkNumber` | 🔵 recognized |
| `llDetectedName` | 🔵 recognized |
| `llDetectedOwner` | 🔵 recognized |
| `llDetectedPos` | 🔵 recognized |
| `llDetectedRezzer` | 🔵 recognized |
| `llDetectedRot` | 🔵 recognized |
| `llDetectedTouchBinormal` | 🔵 recognized |
| `llDetectedTouchFace` | 🔵 recognized |
| `llDetectedTouchNormal` | 🔵 recognized |
| `llDetectedTouchPos` | 🔵 recognized |
| `llDetectedTouchST` | 🔵 recognized |
| `llDetectedTouchUV` | 🔵 recognized |
| `llDetectedType` | 🔵 recognized |
| `llDetectedVel` | 🔵 recognized |
| `llDialog` | 🔵 recognized |
| `llDie` | 🔵 recognized |
| `llDumpList2String` | ✅ implemented |
| `llEdgeOfWorld` | 🔵 recognized |
| `llEjectFromLand` | 🔵 recognized |
| `llEmail` | 🔵 recognized |
| `llEscapeURL` | ✅ implemented |
| `llEuler2Rot` | ✅ implemented |
| `llEvade` | 🔵 recognized |
| `llExecCharacterCmd` | 🔵 recognized |
| `llFabs` | ✅ implemented |
| `llFindNotecardTextSync` | 🔵 recognized |
| `llFleeFrom` | 🔵 recognized |
| `llFloor` | ✅ implemented |
| `llForceMouselook` | 🔵 recognized |
| `llFrand` | 🔵 recognized |
| `llGenerateKey` | ✅ implemented |
| `llGetAccel` | 🔵 recognized |
| `llGetAgentInfo` | 🔵 recognized |
| `llGetAgentLanguage` | 🔵 recognized |
| `llGetAgentList` | 🔵 recognized |
| `llGetAgentSize` | 🔵 recognized |
| `llGetAlpha` | 🔵 recognized |
| `llGetAndResetTime` | 🔵 recognized |
| `llGetAnimation` | 🔵 recognized |
| `llGetAnimationList` | 🔵 recognized |
| `llGetAnimationOverride` | 🔵 recognized |
| `llGetAttached` | 🔵 recognized |
| `llGetAttachedList` | 🔵 recognized |
| `llGetAttachedListFiltered` | 🔵 recognized |
| `llGetBoundingBox` | 🔵 recognized |
| `llGetCameraAspect` | 🔵 recognized |
| `llGetCameraFOV` | 🔵 recognized |
| `llGetCameraPos` | 🔵 recognized |
| `llGetCameraRot` | 🔵 recognized |
| `llGetCenterOfMass` | 🔵 recognized |
| `llGetClosestNavPoint` | 🔵 recognized |
| `llGetColor` | 🔵 recognized |
| `llGetCreator` | 🔵 recognized |
| `llGetDate` | ✅ implemented |
| `llGetDayLength` | 🔵 recognized |
| `llGetDayOffset` | 🔵 recognized |
| `llGetDisplayName` | 🔵 recognized |
| `llGetEnergy` | 🔵 recognized |
| `llGetEnv` | 🔵 recognized |
| `llGetEnvironment` | 🔵 recognized |
| `llGetExperienceDetails` | 🔵 recognized |
| `llGetExperienceErrorMessage` | 🔵 recognized |
| `llGetForce` | 🔵 recognized |
| `llGetFreeMemory` | 🔵 recognized |
| `llGetFreeURLs` | 🔵 recognized |
| `llGetGameControlMode` | 🔵 recognized |
| `llGetGameControlModeAxes` | 🔵 recognized |
| `llGetGameControlModeButtons` | 🔵 recognized |
| `llGetGeometricCenter` | 🔵 recognized |
| `llGetGMTclock` | 🔵 recognized |
| `llGetHealth` | 🔵 recognized |
| `llGetHTTPHeader` | 🔵 recognized |
| `llGetInventoryAcquireTime` | 🔵 recognized |
| `llGetInventoryCreator` | 🔵 recognized |
| `llGetInventoryDesc` | 🔵 recognized |
| `llGetInventoryKey` | 🔵 recognized |
| `llGetInventoryName` | 🔵 recognized |
| `llGetInventoryNumber` | 🔵 recognized |
| `llGetInventoryPermMask` | 🔵 recognized |
| `llGetInventoryType` | 🔵 recognized |
| `llGetKey` | 🔵 recognized |
| `llGetLandOwnerAt` | 🔵 recognized |
| `llGetLinkKey` | 🔵 recognized |
| `llGetLinkMedia` | 🔵 recognized |
| `llGetLinkName` | 🔵 recognized |
| `llGetLinkNumber` | 🔵 recognized |
| `llGetLinkNumberOfSides` | 🔵 recognized |
| `llGetLinkPrimitiveParams` | 🔵 recognized |
| `llGetLinkSitFlags` | 🔵 recognized |
| `llGetListEntryType` | 🔵 recognized |
| `llGetListLength` | ✅ implemented |
| `llGetLocalPos` | 🔵 recognized |
| `llGetLocalRot` | 🔵 recognized |
| `llGetMass` | 🔵 recognized |
| `llGetMassMKS` | 🔵 recognized |
| `llGetMaxScaleFactor` | 🔵 recognized |
| `llGetMemoryLimit` | 🔵 recognized |
| `llGetMinScaleFactor` | 🔵 recognized |
| `llGetMoonDirection` | 🔵 recognized |
| `llGetMoonRotation` | 🔵 recognized |
| `llGetNextEmail` | 🔵 recognized |
| `llGetNotecardLine` | 🔵 recognized |
| `llGetNotecardLineSync` | 🔵 recognized |
| `llGetNumberOfNotecardLines` | 🔵 recognized |
| `llGetNumberOfPrims` | 🔵 recognized |
| `llGetNumberOfSides` | 🔵 recognized |
| `llGetObjectAnimationNames` | 🔵 recognized |
| `llGetObjectDesc` | 🔵 recognized |
| `llGetObjectDetails` | 🔵 recognized |
| `llGetObjectLinkKey` | 🔵 recognized |
| `llGetObjectMass` | 🔵 recognized |
| `llGetObjectName` | 🔵 recognized |
| `llGetObjectPermMask` | 🔵 recognized |
| `llGetObjectPrimCount` | 🔵 recognized |
| `llGetOmega` | 🔵 recognized |
| `llGetOwner` | 🔵 recognized |
| `llGetOwnerKey` | 🔵 recognized |
| `llGetParcelDetails` | 🔵 recognized |
| `llGetParcelFlags` | 🔵 recognized |
| `llGetParcelMaxPrims` | 🔵 recognized |
| `llGetParcelMusicURL` | 🔵 recognized |
| `llGetParcelPrimCount` | 🔵 recognized |
| `llGetParcelPrimOwners` | 🔵 recognized |
| `llGetPermissions` | 🔵 recognized |
| `llGetPermissionsKey` | 🔵 recognized |
| `llGetPhysicsMaterial` | 🔵 recognized |
| `llGetPos` | 🔵 recognized |
| `llGetPrimitiveParams` | 🔵 recognized |
| `llGetPrimMediaParams` | 🔵 recognized |
| `llGetRegionAgentCount` | 🔵 recognized |
| `llGetRegionCorner` | 🔵 recognized |
| `llGetRegionDayLength` | 🔵 recognized |
| `llGetRegionDayOffset` | 🔵 recognized |
| `llGetRegionFlags` | 🔵 recognized |
| `llGetRegionFPS` | 🔵 recognized |
| `llGetRegionMoonDirection` | 🔵 recognized |
| `llGetRegionMoonRotation` | 🔵 recognized |
| `llGetRegionName` | 🔵 recognized |
| `llGetRegionSunDirection` | 🔵 recognized |
| `llGetRegionSunRotation` | 🔵 recognized |
| `llGetRegionTimeDilation` | 🔵 recognized |
| `llGetRegionTimeOfDay` | 🔵 recognized |
| `llGetRenderMaterial` | 🔵 recognized |
| `llGetRootPosition` | 🔵 recognized |
| `llGetRootRotation` | 🔵 recognized |
| `llGetRot` | 🔵 recognized |
| `llGetScale` | 🔵 recognized |
| `llGetScriptName` | 🔵 recognized |
| `llGetScriptState` | 🔵 recognized |
| `llGetSimStats` | 🔵 recognized |
| `llGetSimulatorHostname` | 🔵 recognized |
| `llGetSPMaxMemory` | 🔵 recognized |
| `llGetStartParameter` | 🔵 recognized |
| `llGetStartString` | 🔵 recognized |
| `llGetStaticPath` | 🔵 recognized |
| `llGetStatus` | 🔵 recognized |
| `llGetSubString` | 🟡 partial |
| `llGetSunDirection` | 🔵 recognized |
| `llGetSunRotation` | 🔵 recognized |
| `llGetTexture` | 🔵 recognized |
| `llGetTextureOffset` | 🔵 recognized |
| `llGetTextureRot` | 🔵 recognized |
| `llGetTextureScale` | 🔵 recognized |
| `llGetTime` | 🔵 recognized |
| `llGetTimeOfDay` | 🔵 recognized |
| `llGetTimestamp` | ✅ implemented |
| `llGetTorque` | 🔵 recognized |
| `llGetUnixTime` | ✅ implemented |
| `llGetUsedMemory` | 🔵 recognized |
| `llGetUsername` | 🔵 recognized |
| `llGetVel` | 🔵 recognized |
| `llGetVisualParams` | 🔵 recognized |
| `llGetWallclock` | 🔵 recognized |
| `llGiveAgentInventory` | 🔵 recognized |
| `llGiveInventory` | 🔵 recognized |
| `llGiveInventoryList` | 🔵 recognized |
| `llGiveMoney` | 🔵 recognized |
| `llGodLikeRezObject` | ⛔ unsupported |
| `llGround` | 🔵 recognized |
| `llGroundContour` | 🔵 recognized |
| `llGroundNormal` | 🔵 recognized |
| `llGroundRepel` | 🔵 recognized |
| `llGroundSlope` | 🔵 recognized |
| `llHash` | 🔵 recognized |
| `llHMAC` | 🔵 recognized |
| `llHTTPRequest` | 🔵 recognized |
| `llHTTPResponse` | 🔵 recognized |
| `llInsertString` | 🟡 partial |
| `llInstantMessage` | 🟡 partial |
| `llIntegerToBase64` | ✅ implemented |
| `llIsFriend` | 🔵 recognized |
| `llIsLinkGLTFMaterial` | 🔵 recognized |
| `llJson2List` | 🔵 recognized |
| `llJsonGetValue` | 🔵 recognized |
| `llJsonSetValue` | 🔵 recognized |
| `llJsonValueType` | 🔵 recognized |
| `llKey2Name` | 🔵 recognized |
| `llKeyCountKeyValue` | 🔵 recognized |
| `llKeysKeyValue` | 🔵 recognized |
| `llLinear2sRGB` | 🔵 recognized |
| `llLinkAdjustSoundVolume` | 🔵 recognized |
| `llLinkParticleSystem` | 🔵 recognized |
| `llLinkPlaySound` | 🔵 recognized |
| `llLinksetDataAvailable` | 🔵 recognized |
| `llLinksetDataCountFound` | 🔵 recognized |
| `llLinksetDataCountKeys` | 🔵 recognized |
| `llLinksetDataDelete` | 🔵 recognized |
| `llLinksetDataDeleteFound` | 🔵 recognized |
| `llLinksetDataDeleteProtected` | 🔵 recognized |
| `llLinksetDataFindKeys` | 🔵 recognized |
| `llLinksetDataListKeys` | 🔵 recognized |
| `llLinksetDataRead` | 🔵 recognized |
| `llLinksetDataReadProtected` | 🔵 recognized |
| `llLinksetDataReset` | 🔵 recognized |
| `llLinksetDataWrite` | 🔵 recognized |
| `llLinksetDataWriteProtected` | 🔵 recognized |
| `llLinkSetSoundQueueing` | 🔵 recognized |
| `llLinkSetSoundRadius` | 🔵 recognized |
| `llLinkSitTarget` | 🔵 recognized |
| `llLinkStopSound` | 🔵 recognized |
| `llList2CSV` | ✅ implemented |
| `llList2Float` | ✅ implemented |
| `llList2Integer` | ✅ implemented |
| `llList2Json` | 🔵 recognized |
| `llList2Key` | ✅ implemented |
| `llList2List` | ✅ implemented |
| `llList2ListSlice` | 🔵 recognized |
| `llList2ListStrided` | 🔵 recognized |
| `llList2Rot` | ✅ implemented |
| `llList2String` | ✅ implemented |
| `llList2Vector` | ✅ implemented |
| `llListen` | 🟡 partial |
| `llListenControl` | 🔵 recognized |
| `llListenRemove` | 🔵 recognized |
| `llListFindList` | ✅ implemented |
| `llListFindListNext` | 🔵 recognized |
| `llListFindStrided` | 🔵 recognized |
| `llListInsertList` | ✅ implemented |
| `llListRandomize` | 🔵 recognized |
| `llListReplaceList` | ✅ implemented |
| `llListSort` | 🔵 recognized |
| `llListSortStrided` | 🔵 recognized |
| `llListStatistics` | 🔵 recognized |
| `llLoadURL` | 🔵 recognized |
| `llLog` | 🟡 partial |
| `llLog10` | 🟡 partial |
| `llLookAt` | 🔵 recognized |
| `llLoopSound` | 🔵 recognized |
| `llLoopSoundMaster` | 🔵 recognized |
| `llLoopSoundSlave` | 🔵 recognized |
| `llMakeExplosion` | ⛔ unsupported |
| `llMakeFire` | ⛔ unsupported |
| `llMakeFountain` | ⛔ unsupported |
| `llMakeSmoke` | ⛔ unsupported |
| `llManageEstateAccess` | 🔵 recognized |
| `llMapBeacon` | 🔵 recognized |
| `llMapDestination` | 🔵 recognized |
| `llMD5String` | 🔵 recognized |
| `llMessageLinked` | 🔵 recognized |
| `llMinEventDelay` | 🔵 recognized |
| `llModifyLand` | 🔵 recognized |
| `llModPow` | 🔵 recognized |
| `llMoveToTarget` | 🔵 recognized |
| `llName2Key` | 🔵 recognized |
| `llNavigateTo` | 🔵 recognized |
| `llOffsetTexture` | 🔵 recognized |
| `llOpenFloater` | ⛔ unsupported |
| `llOpenRemoteDataChannel` | ⛔ unsupported |
| `llOrd` | 🟡 partial |
| `llOverMyLand` | 🔵 recognized |
| `llOwnerSay` | 🟡 partial |
| `llParcelMediaCommandList` | 🔵 recognized |
| `llParcelMediaQuery` | 🔵 recognized |
| `llParseString2List` | 🔵 recognized |
| `llParseStringKeepNulls` | 🔵 recognized |
| `llParticleSystem` | 🔵 recognized |
| `llPassCollisions` | 🔵 recognized |
| `llPassTouches` | 🔵 recognized |
| `llPatrolPoints` | 🔵 recognized |
| `llPlaySound` | 🔵 recognized |
| `llPlaySoundSlave` | 🔵 recognized |
| `llPointAt` | ⛔ unsupported |
| `llPow` | ✅ implemented |
| `llPreloadSound` | 🔵 recognized |
| `llPursue` | 🔵 recognized |
| `llPushObject` | 🔵 recognized |
| `llReadKeyValue` | 🔵 recognized |
| `llRefreshPrimURL` | ⛔ unsupported |
| `llRegionSay` | 🔵 recognized |
| `llRegionSayTo` | 🔵 recognized |
| `llReleaseCamera` | ⛔ unsupported |
| `llReleaseControls` | 🔵 recognized |
| `llReleaseURL` | 🔵 recognized |
| `llRemoteDataReply` | ⛔ unsupported |
| `llRemoteDataSetRegion` | ⛔ unsupported |
| `llRemoteLoadScript` | ⛔ unsupported |
| `llRemoteLoadScriptPin` | 🔵 recognized |
| `llRemoveFromLandBanList` | 🔵 recognized |
| `llRemoveFromLandPassList` | 🔵 recognized |
| `llRemoveInventory` | 🔵 recognized |
| `llRemoveVehicleFlags` | 🔵 recognized |
| `llReplaceAgentEnvironment` | 🔵 recognized |
| `llReplaceEnvironment` | 🔵 recognized |
| `llReplaceSubString` | 🔵 recognized |
| `llRequestAgentData` | 🔵 recognized |
| `llRequestDisplayName` | 🔵 recognized |
| `llRequestExperiencePermissions` | 🔵 recognized |
| `llRequestInventoryData` | 🔵 recognized |
| `llRequestPermissions` | 🔵 recognized |
| `llRequestSecureURL` | 🔵 recognized |
| `llRequestSimulatorData` | 🔵 recognized |
| `llRequestURL` | 🔵 recognized |
| `llRequestUserKey` | 🔵 recognized |
| `llRequestUsername` | 🔵 recognized |
| `llResetAnimationOverride` | 🔵 recognized |
| `llResetLandBanList` | 🔵 recognized |
| `llResetLandPassList` | 🔵 recognized |
| `llResetOtherScript` | 🔵 recognized |
| `llResetScript` | 🟡 partial |
| `llResetTime` | 🔵 recognized |
| `llReturnObjectsByID` | 🔵 recognized |
| `llReturnObjectsByOwner` | 🔵 recognized |
| `llRezAtRoot` | 🔵 recognized |
| `llRezObject` | 🔵 recognized |
| `llRezObjectWithParams` | 🔵 recognized |
| `llRot2Angle` | ✅ implemented |
| `llRot2Axis` | ✅ implemented |
| `llRot2Euler` | ✅ implemented |
| `llRot2Fwd` | ✅ implemented |
| `llRot2Left` | ✅ implemented |
| `llRot2Up` | ✅ implemented |
| `llRotateTexture` | 🔵 recognized |
| `llRotBetween` | ✅ implemented |
| `llRotLookAt` | 🔵 recognized |
| `llRotTarget` | 🔵 recognized |
| `llRotTargetRemove` | 🔵 recognized |
| `llRound` | ✅ implemented |
| `llSameGroup` | 🔵 recognized |
| `llSay` | 🟡 partial |
| `llScaleByFactor` | 🔵 recognized |
| `llScaleTexture` | 🔵 recognized |
| `llScriptDanger` | 🔵 recognized |
| `llScriptProfiler` | 🔵 recognized |
| `llSendRemoteData` | ⛔ unsupported |
| `llSensor` | 🔵 recognized |
| `llSensorRemove` | 🔵 recognized |
| `llSensorRepeat` | 🔵 recognized |
| `llSetAgentEnvironment` | 🔵 recognized |
| `llSetAgentRot` | 🔵 recognized |
| `llSetAlpha` | 🔵 recognized |
| `llSetAngularVelocity` | 🟡 partial |
| `llSetAnimationOverride` | 🔵 recognized |
| `llSetBuoyancy` | 🔵 recognized |
| `llSetCameraAtOffset` | 🔵 recognized |
| `llSetCameraEyeOffset` | 🔵 recognized |
| `llSetCameraParams` | 🔵 recognized |
| `llSetClickAction` | 🔵 recognized |
| `llSetColor` | 🔵 recognized |
| `llSetContentType` | 🔵 recognized |
| `llSetDamage` | 🔵 recognized |
| `llSetEnvironment` | 🔵 recognized |
| `llSetForce` | 🔵 recognized |
| `llSetForceAndTorque` | 🔵 recognized |
| `llSetLinkGLTFOverrides` | 🔵 recognized |
| `llSetGroundTexture` | 🔵 recognized |
| `llSetHoverHeight` | 🔵 recognized |
| `llSetInventoryPermMask` | ⛔ unsupported |
| `llSetKeyframedMotion` | 🔵 recognized |
| `llSetLinkAlpha` | 🔵 recognized |
| `llSetLinkCamera` | 🔵 recognized |
| `llSetLinkColor` | 🔵 recognized |
| `llSetLinkMedia` | 🔵 recognized |
| `llSetLinkPrimitiveParams` | 🔵 recognized |
| `llSetLinkPrimitiveParamsFast` | 🔵 recognized |
| `llSetLinkRenderMaterial` | 🔵 recognized |
| `llSetLinkSitFlags` | 🔵 recognized |
| `llSetLinkTexture` | 🔵 recognized |
| `llSetLinkTextureAnim` | 🔵 recognized |
| `llSetLocalRot` | 🔵 recognized |
| `llSetMemoryLimit` | 🔵 recognized |
| `llSetObjectDesc` | 🔵 recognized |
| `llSetObjectName` | 🔵 recognized |
| `llSetObjectPermMask` | ⛔ unsupported |
| `llSetParcelMusicURL` | 🔵 recognized |
| `llSetPayPrice` | 🔵 recognized |
| `llSetPhysicsMaterial` | 🔵 recognized |
| `llSetPos` | 🟡 partial |
| `llSetPrimitiveParams` | 🔵 recognized |
| `llSetPrimMediaParams` | 🔵 recognized |
| `llSetPrimURL` | ⛔ unsupported |
| `llSetRegionPos` | 🟡 partial |
| `llSetRemoteScriptAccessPin` | 🔵 recognized |
| `llSetRenderMaterial` | 🔵 recognized |
| `llSetRot` | 🔵 recognized |
| `llSetScale` | 🟡 partial |
| `llSetScriptState` | 🔵 recognized |
| `llSetSitText` | 🔵 recognized |
| `llSetSoundQueueing` | 🔵 recognized |
| `llSetSoundRadius` | 🔵 recognized |
| `llSetStatus` | 🟡 partial |
| `llSetText` | 🟡 partial |
| `llSetTexture` | 🔵 recognized |
| `llSetTextureAnim` | 🔵 recognized |
| `llSetTimerEvent` | 🟡 partial |
| `llSetTorque` | 🔵 recognized |
| `llSetTouchText` | 🔵 recognized |
| `llSetVehicleFlags` | 🔵 recognized |
| `llSetVehicleFloatParam` | 🔵 recognized |
| `llSetVehicleRotationParam` | 🔵 recognized |
| `llSetVehicleType` | 🔵 recognized |
| `llSetVehicleVectorParam` | 🔵 recognized |
| `llSetVelocity` | 🟡 partial |
| `llSHA1String` | ✅ implemented |
| `llSHA256String` | ✅ implemented |
| `llShout` | 🟡 partial |
| `llSignRSA` | 🔵 recognized |
| `llSin` | ✅ implemented |
| `llSitOnLink` | 🔵 recognized |
| `llSitTarget` | 🔵 recognized |
| `llSleep` | 🔵 recognized |
| `llSound` | ⛔ unsupported |
| `llSoundPreload` | ⛔ unsupported |
| `llSqrt` | 🟡 partial |
| `llsRGB2Linear` | 🔵 recognized |
| `llStartAnimation` | 🔵 recognized |
| `llStartObjectAnimation` | 🔵 recognized |
| `llStopAnimation` | 🔵 recognized |
| `llStopObjectAnimation` | 🔵 recognized |
| `llStopHover` | 🔵 recognized |
| `llStopLookAt` | 🔵 recognized |
| `llStopMoveToTarget` | 🔵 recognized |
| `llStopPointAt` | ⛔ unsupported |
| `llStopSound` | 🔵 recognized |
| `llStringLength` | ✅ implemented |
| `llStringToBase64` | ✅ implemented |
| `llStringTrim` | 🟡 partial |
| `llSubStringIndex` | ✅ implemented |
| `llTakeCamera` | ⛔ unsupported |
| `llTakeControls` | 🔵 recognized |
| `llTan` | ✅ implemented |
| `llTarget` | 🔵 recognized |
| `llTargetedEmail` | 🔵 recognized |
| `llTargetOmega` | 🔵 recognized |
| `llTargetRemove` | 🔵 recognized |
| `llTeleportAgent` | 🔵 recognized |
| `llTeleportAgentGlobalCoords` | 🔵 recognized |
| `llTeleportAgentHome` | 🔵 recognized |
| `llTextBox` | 🔵 recognized |
| `llToLower` | ✅ implemented |
| `llToUpper` | ✅ implemented |
| `llTransferLindenDollars` | 🔵 recognized |
| `llTransferOwnership` | 🔵 recognized |
| `llTriggerSound` | 🔵 recognized |
| `llTriggerSoundLimited` | 🔵 recognized |
| `llUnescapeURL` | ✅ implemented |
| `llUnSit` | 🔵 recognized |
| `llUpdateCharacter` | 🔵 recognized |
| `llUpdateKeyValue` | 🔵 recognized |
| `llVecDist` | ✅ implemented |
| `llVecMag` | ✅ implemented |
| `llVecNorm` | ✅ implemented |
| `llVerifyRSA` | 🔵 recognized |
| `llVolumeDetect` | 🔵 recognized |
| `llWanderWithin` | 🔵 recognized |
| `llWater` | 🔵 recognized |
| `llWhisper` | 🟡 partial |
| `llWind` | 🔵 recognized |
| `llWorldPosToHUD` | 🔵 recognized |
| `llXorBase64` | 🔵 recognized |
| `llXorBase64Strings` | ⛔ unsupported |
| `llXorBase64StringsCorrect` | ⛔ unsupported |


## 9.0 Grenzen

Die obige Tabelle ist absichtlich streng. Ein 🔵-Eintrag bedeutet nicht, dass ein gleichnamiger Funktionsaufruf stillschweigend akzeptiert wird. Nicht implementierte Funktionen sollen beim Kompilieren/Ausführen einen expliziten Fehler liefern, statt falsche Second-Life-Semantik vorzutäuschen.

Die nächsten LSL-Schwerpunkte sind Ausdrucks-/Control-Flow-Vollständigkeit, globale Variablen und User-Funktionen, synchrone World-Reads, Inventory/Asset/Linkset-Funktionen, Sensor/Collision/Touch-Eventquellen, HTTP/DataServer sowie Permission/Experience-Workflows.


## 11.0 Hinweise

- OGL v2 führt die vier zuvor fehlenden Sprachmerkmale aus: Kontrollfluss mit `if/else`, budgetierte `while/for`-Schleifen, wiederverwendbare `function/call`-Prozeduren und typisierte Deklarationen.
- Schleifen und Sprünge laufen im Shared IR und bleiben durch das normale VM-Instruktionsbudget begrenzt.
- LSL erhielt 35 zusätzliche strikt ausführbare deterministische Builtins.
- Die LSL-Prozentwerte bleiben bewusst konservativ. World-, Viewer-, Medien-, Experience-, HTTP-, Vehicle- und weitere servicegebundene Funktionen werden erst als ausführbar gezählt, wenn ihre Serversemantik tatsächlich vorhanden und getestet ist.
- Vollständige Katalogisierung (523 Funktionen / 44 Event-Kategorien) ist weiterhin nicht mit vollständiger Second-Life-Semantik gleichzusetzen.
