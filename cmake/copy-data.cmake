FUNCTION(copy_data data_glob)
	file(GLOB_RECURSE DATAS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "${data_glob}")
	FOREACH(DATA_FILE ${DATAS})
		GET_FILENAME_COMPONENT(copy-dest-dir ${CMAKE_CURRENT_BINARY_DIR}/${DATA_FILE} DIRECTORY)
		SET(copy-output ${CMAKE_CURRENT_BINARY_DIR}/${DATA_FILE})
		ADD_CUSTOM_COMMAND(
		   OUTPUT  ${copy-output}
		   COMMAND ${CMAKE_COMMAND} -E make_directory ${copy-dest-dir}
		   COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${DATA_FILE}" "${copy-output}"
		   COMMENT "Copying ${DATA_FILE} to ${copy-output}"
		   DEPENDS ${DATA_FILE}
		   VERBATIM
		)
	ENDFOREACH()
	add_custom_target("${EXAMPLE_NAME}_DataCopy" ALL DEPENDS ${DATAS})
ENDFUNCTION()
