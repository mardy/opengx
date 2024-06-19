set(TEX_SOURCES
)

set(IMAGES
    case1.png
    case2.png
    case3.png
    case4.png
    inputs.png
    light.png
    lm_sample.png
    nolight.png
    outputs.png
    pipeline.png
    stages.png
    tev_sample1.png
    tev_sample2.png
)

add_latex_document(opengx.tex
    INPUTS ${TEX_SOURCES}
    IMAGES ${IMAGES}
    TARGET_NAME opengxdoc
    FORCE_PDF
)
