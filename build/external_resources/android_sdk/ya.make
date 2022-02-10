RESOURCES_LIBRARY() 
 
OWNER(g:mapkit) 
 
IF (OS_ANDROID) 
    # Android SDK for linux and darwin: Build-Tools 30.0.3, Platform 30
    DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE( 
        ANDROID_SDK 
        sbr:2564045529 FOR LINUX
        sbr:2564523615 FOR DARWIN
    ) 
    IF (NOT HOST_OS_LINUX AND NOT HOST_OS_DARWIN) 
        MESSAGE(FATAL_ERROR Unsupported platform for ANDROID_SDK) 
    ENDIF() 
    DECLARE_EXTERNAL_RESOURCE(ANDROID_AVD sbr:2563860055)
ELSE() 
    MESSAGE(FATAL_ERROR Unsupported platform) 
ENDIF() 
 
END() 
