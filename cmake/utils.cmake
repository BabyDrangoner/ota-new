function (force_redefine_file_macro_for_sources targetname)
    get_target_property(source_files "${targetname}" SOURCES)
    foreach(sourcefile ${source_files})
        get_property(defs SOURCE "${sourcefile}" PROPERTY COMPILE_DEFINITIONS)
        get_filename_component(filepath "${sourcefile}" ABSOLUTE)
        string(REPLACE ${PROJECT_SOURCE_DIR}/ "" relpath ${filepath})
        list(APPEND defs "__FILE__=\"${relpath}\"")
        set_property(
            SOURCE "${sourcefile}"
            PROPERTY COMPILE_DEFINITIONS ${defs}
        )
    endforeach()
endfunction()

function(ragelmaker src_rl var output_dir)
    get_filename_component(src_file ${src_rl} NAME_WE)
    set(rl_out ${output_dir}/${src_file}.rl.cc)
    get_filename_component(abs_src_rl ${src_rl} ABSOLUTE)
    
    add_custom_command(
        OUTPUT ${rl_out}
        COMMAND ragel ${abs_src_rl} -o ${rl_out} -l -C -G2
        DEPENDS ${abs_src_rl}
        COMMENT "Compiling Ragel file ${src_rl}"
    )
    
    set(${var} ${${var}} ${rl_out} PARENT_SCOPE)
    set_source_files_properties(${rl_out} PROPERTIES GENERATED TRUE)
endfunction()
