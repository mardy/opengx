set(TEX_SOURCES
)

set(SVGS
    case1.svg
    case2.svg
    case3.svg
    case4.svg
    inputs.svg
    light.svg
    lm_sample.svg
    nolight.svg
    outputs.svg
    pipeline.svg
    stages.svg
    tev_sample1.svg
    tev_sample2.svg
)

function(convert_svgs out_var)
    set(result)
    foreach(in_f ${ARGN})
        string(REPLACE ".svg" ".png" out_f ${in_f})

        add_custom_command(
            OUTPUT ${out_f}
            COMMAND convert -density 425 -background none -trim +repage ${CMAKE_CURRENT_SOURCE_DIR}/${in_f} ${out_f}
            DEPENDS ${in_f}
            COMMENT "Converting ${in_f} to ${out_f}"
        )
        list(APPEND result ${out_f})
    endforeach()
    set(${out_var} ${result} PARENT_SCOPE)
endfunction()

convert_svgs(IMAGES ${SVGS})

add_latex_document(opengx.tex
    INPUTS ${TEX_SOURCES}
    IMAGES ${IMAGES}
    DEPENDS ${IMAGES}
    TARGET_NAME opengxdoc
    FORCE_PDF
)
