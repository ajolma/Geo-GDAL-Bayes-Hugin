#include <cpl_conv.h>
#include <gdal.h>

#include "hugin.h"

typedef h_domain_t Hugin_Domain;

typedef struct {
    int n_evidence_nodes;
    char **evidence_nodes;
    int *evidence_offsets;
    GDALRasterBandH *evidence_bands;
    SV **evidence_band_svs;
    
    char *output_node;
    int output_from_state;
    GDALRasterBandH output_band;
    SV *output_band_sv;
    
    Hugin_Domain domain;
    SV *domain_sv;
    
    int width, height;
} Geo_GDAL_Bayes_Hugin_t;

typedef Geo_GDAL_Bayes_Hugin_t* Geo_GDAL_Bayes_Hugin;

Geo_GDAL_Bayes_Hugin create(HV *setup);

void destroy(Geo_GDAL_Bayes_Hugin self);

void compute(Geo_GDAL_Bayes_Hugin self);
