IF (DOXYGEN_FOUND)
	EXECUTE_PROCESS(
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		# COMMAND bzr nick
		# This will get the current branch name; I think that is what is wanted?
		COMMAND git rev-parse --abbrev-ref HEAD
		OUTPUT_VARIABLE BZR_NICK OUTPUT_STRIP_TRAILING_WHITESPACE)
	EXECUTE_PROCESS(
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		# I dunno, git doesn't have anything analogous to bzr revno...
		# COMMAND bzr revno
		# COMMAND git describe
		COMMAND git rev-parse HEAD
		OUTPUT_VARIABLE BZR_REVNO OUTPUT_STRIP_TRAILING_WHITESPACE)
	CONFIGURE_FILE(
		${PROJECT_SOURCE_DIR}/doc/doxygen.conf.in
		${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
	ADD_CUSTOM_TARGET (
		doxygen
		COMMAND ${DOXYGEN} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMENT "Generating documentation"
	)
    IF (STACK_FOUND)
        ADD_DEPENDENCIES(doxygen haskell-doc)
    ENDIF (STACK_FOUND)
    ADD_CUSTOM_TARGET(haskell-doc
        COMMAND cabal configure --builddir=${CMAKE_BINARY_DIR}/opencog/haskell
                      --extra-lib-dirs=${CMAKE_BINARY_DIR}/opencog/haskell
        COMMAND cabal haddock --builddir=${CMAKE_BINARY_DIR}/opencog/haskell --html
                      --haddock-option=-o${CMAKE_CURRENT_BINARY_DIR}/html/haskell
        COMMENT "Generating haskell library documentation..."
        DEPENDS haskell-atomspace-lib
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/opencog/haskell/
    )
ELSE (DOXYGEN_FOUND)
    MESSAGE(STATUS "Doxygen not found, you won't be able to generate API documentation.")
ENDIF (DOXYGEN_FOUND)
