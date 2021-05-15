if(WITH_QT6)
	find_package(Qt6 COMPONENTS Core Sql REQUIRED)
else()
	find_package(Qt5 COMPONENTS Core Sql REQUIRED)
endif()

set(CMAKE_AUTOMOC TRUE)

set(qdjango_db_HEADERS
	QDjango.h
	QDjango_p.h
	QDjangoMetaModel.h
	QDjangoModel.h
	QDjangoQuerySet.h
	QDjangoQuerySet_p.h
	QDjangoWhere.h
	QDjangoWhere_p.h
	)

add_library(qdjango-db SHARED
	QDjango.cpp
	QDjangoMetaModel.cpp
	QDjangoModel.cpp
	QDjangoQuerySet.cpp
	QDjangoWhere.cpp
	${qdjango_db_HEADERS}
	)

if(WITH_QT6)
	target_link_libraries(qdjango-db Qt6::Core Qt6::Sql)
else()
	target_link_libraries(qdjango-db Qt5::Core Qt5::Sql)
endif()

target_include_directories(qdjango-db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(qdjango-db PRIVATE
	QT_NO_CAST_FROM_ASCII
	QT_NO_CAST_TO_ASCII
	QDJANGO_DB_BUILD
	)

set_target_properties(qdjango-db PROPERTIES
	LINK_FLAGS "-Wl,-no-undefined"
	SOVERSION 0.6.2
	)

install(TARGETS qdjango-db LIBRARY)
install(FILES ${qdjango_db_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qdjango/db)
