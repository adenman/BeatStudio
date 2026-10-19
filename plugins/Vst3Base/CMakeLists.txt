# VST3 hosting support shared by the vst3instrument and vst3effect plugins.
# Only Windows is supported for now: plugin editors are embedded through HWNDs.
if(NOT LMMS_BUILD_WIN32)
	return()
endif()

# Steinberg VST3 SDK (MIT licensed since 3.8.0). Only the parts needed for hosting are fetched.
include(FetchContent)
set(VST3_SDK_TAG "v3.8.0_build_66")
set(VST3_SDK_ROOT "${CMAKE_CURRENT_BINARY_DIR}/vst3sdk")
set(VST3_SDK_PARTS
	"base|base|001ef3dccf89505ee624766c8140c6c845fbd214460e02ea16145db29f1e2ed6"
	"pluginterfaces|pluginterfaces|c922f0a35f2c093dfcb24ccd95282e70d00cc2de42fc0c8ac1577398ffe7b6d4"
	"public_sdk|public.sdk|83b64e6666a54a35a2292a73ef35048b6d0dadd5e50a22cb437ac4cfb69ba81f"
)
foreach(part IN LISTS VST3_SDK_PARTS)
	string(REPLACE "|" ";" part "${part}")
	list(GET part 0 repo)
	list(GET part 1 dir)
	list(GET part 2 hash)
	FetchContent_Declare("vst3_${repo}"
		URL "https://github.com/steinbergmedia/vst3_${repo}/archive/refs/tags/${VST3_SDK_TAG}.tar.gz"
		URL_HASH "SHA256=${hash}"
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
		SOURCE_DIR "${VST3_SDK_ROOT}/${dir}"
	)
	FetchContent_MakeAvailable("vst3_${repo}")
endforeach()

add_library(vst3sdk STATIC
	"${VST3_SDK_ROOT}/pluginterfaces/base/coreiids.cpp"
	"${VST3_SDK_ROOT}/pluginterfaces/base/funknown.cpp"
	"${VST3_SDK_ROOT}/pluginterfaces/base/ustring.cpp"
	"${VST3_SDK_ROOT}/base/source/baseiids.cpp"
	"${VST3_SDK_ROOT}/base/source/fdebug.cpp"
	"${VST3_SDK_ROOT}/base/source/fobject.cpp"
	"${VST3_SDK_ROOT}/base/source/fstring.cpp"
	"${VST3_SDK_ROOT}/base/source/updatehandler.cpp"
	"${VST3_SDK_ROOT}/base/thread/source/flock.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/common/commoniids.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/common/commonstringconvert.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/common/threadchecker_win32.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/utility/stringconvert.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/connectionproxy.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/eventlist.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/parameterchanges.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp"
	"${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/processdata.cpp"
)
# third-party code: build without warnings (see ErrorFlags.cmake)
set_target_properties(vst3sdk PROPERTIES SYSTEM TRUE CXX_STANDARD 17)
target_include_directories(vst3sdk SYSTEM PUBLIC "${VST3_SDK_ROOT}")
target_compile_definitions(vst3sdk PUBLIC
	"$<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>"
	UNICODE
	_UNICODE
)
target_link_libraries(vst3sdk PUBLIC ole32 shell32 uuid)

add_library(vst3base STATIC
	Vst3Host.cpp
	Vst3Host.h
	Vst3PluginPanel.cpp
	Vst3PluginPanel.h
	Vst3SubPluginFeatures.cpp
	Vst3SubPluginFeatures.h
)
set_target_properties(vst3base PROPERTIES CXX_STANDARD 20)
target_include_directories(vst3base PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_BINARY_DIR}"
	"${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(vst3base PUBLIC
	lmms
	vst3sdk
	"Qt${QT_VERSION_MAJOR}::Widgets"
	"Qt${QT_VERSION_MAJOR}::Xml"
)
