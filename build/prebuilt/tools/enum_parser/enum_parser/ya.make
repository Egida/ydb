OWNER(g:ymake) 
 
INCLUDE(ya.make.prebuilt) 
 
IF (NOT PREBUILT) 
    MESSAGE(FATAL_ERROR Unsupported host platform for prebuilt enum_parser tool) 
ENDIF() 
