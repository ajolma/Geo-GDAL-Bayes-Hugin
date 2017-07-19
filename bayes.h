#include <cpl_conv.h>
#include <gdal.h>

#include "hugin.h"

typedef h_domain_t Hugin_Domain;

typedef struct {
    int n;
    char **names;
    int *states;
    int value;
    Hugin_Domain domain;
    SV *domain_sv;
    GDALRasterBandH *bands;
    SV **band_svs;
    int width, height;
} Geo_GDAL_Bayes_Hugin_t;

typedef Geo_GDAL_Bayes_Hugin_t* Geo_GDAL_Bayes_Hugin;

Geo_GDAL_Bayes_Hugin create(HV *setup);

void destroy(Geo_GDAL_Bayes_Hugin self);

void compute(Geo_GDAL_Bayes_Hugin self);
