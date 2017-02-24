if(NOT TARGET allscale)
	include(ExternalProject)

	if(USE_ALLSCALECC)
		if(NOT EXISTS ${THIRD_PARTY_DIR})
			message(FATAL_ERROR
				"======================================================\n"
				"No third_party directory found, did you forget to run:\n"
				"   $ scripts/dependencies/installer\n"
				"   $ scripts/dependencies/third_party_linker\n"
				"======================================= (╯°□°）╯︵ ┻━┻\n"
			)
		endif()

		ExternalProject_Add(
			allscale
			GIT_REPOSITORY https://github.com/allscale/allscale_compiler
			GIT_TAG 64e632afae9c347e96d5e158cc39393142038238
			CMAKE_COMMAND
				${CMAKE_COMMAND} -E env
				"INSIEME_LIBS_HOME=${THIRD_PARTY_DIR}"
				${CMAKE_COMMAND}
				-DINSIEME_C_BACKEND_COMPILER=${CMAKE_C_COMPILER}
				-DINSIEME_CXX_BACKEND_COMPILER=${CMAKE_CXX_COMPILER}
			BUILD_COMMAND $(MAKE) allscalecc
			INSTALL_COMMAND ""
			TEST_COMMAND ""
			EXCLUDE_FROM_ALL 1
			DOWNLOAD_NO_PROGRESS 1
		)
		ExternalProject_Get_Property(allscale source_dir binary_dir)

		set(ALLSCALECC ${binary_dir}/code/allscalecc)
		set(ALLSCALE_API_INCLUDE_PATH ${source_dir}/api/code/include)
	else()
		ExternalProject_Add(
			allscale
			GIT_REPOSITORY https://github.com/allscale/allscale_api
			GIT_TAG fe90452a8d4c65dd14e876a9b647af42499fe016
			BUILD_COMMAND ""
			INSTALL_COMMAND ""
			TEST_COMMAND ""
			EXCLUDE_FROM_ALL 1
			DOWNLOAD_NO_PROGRESS 1
		)
		ExternalProject_Get_Property(allscale source_dir binary_dir)

		set(ALLSCALE_API_INCLUDE_PATH ${source_dir}/code/include)
	endif()
endif()
