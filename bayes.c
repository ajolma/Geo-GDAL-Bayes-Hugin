#include "EXTERN.h"
#include "perl.h"
#undef seed
#undef do_open
#undef do_close

#include "bayes.h"

HV *SvHash(SV **svp) {
    if (svp && SvROK(*svp) &&  SvTYPE(SvRV(*svp)) == SVt_PVHV) {
        return (HV*)SvRV(*svp);
    }
    return NULL;
}

GDALRasterBandH SvBand(SV *sv) {
    if (sv && sv_isobject(sv) && sv_derived_from(sv, "Geo::GDAL::Band")) {
        SV *tsv = (SV*)SvRV(sv);
        MAGIC *mg = mg_find(tsv, 'P');
        tsv = mg->mg_obj;
        tsv = (SV*)SvRV(tsv);
        IV tmp = SvIV(tsv);
        return INT2PTR(GDALRasterBandH, tmp);
    }
    return NULL;
}

Geo_GDAL_Bayes_Hugin create(HV *setup) {
    
    Geo_GDAL_Bayes_Hugin self = (Geo_GDAL_Bayes_Hugin)malloc(sizeof(Geo_GDAL_Bayes_Hugin_t));
    self->n = 0;
    self->names = NULL;
    self->offsets = NULL;
    self->domain = NULL;
    self->domain_sv = NULL;
    self->bands = NULL;
    self->band_svs = NULL;
    self->width = -1;
    self->height = -1;

    // now the number of nodes is the number of keys in setup
    // but that's just now
    const char *key = "evidence";
    if (HV *evidence = SvHash(hv_fetch(setup, key, strlen(key), 0))) {
        hv_iterinit(evidence);
        self->n = 0;
        while (HE* he = hv_iternext(evidence)) {
            self->n += 1;
        }
        self->n += 1; // the last one will be output
        self->names = (char **)calloc(sizeof(char*), self->n);
        self->offsets = (int *)calloc(sizeof(int), self->n);
        self->bands = (GDALRasterBandH *)calloc(sizeof(GDALRasterBandH*), self->n);
        self->band_svs = (SV **)calloc(sizeof(SV*), self->n);
        hv_iterinit(evidence);
        int i = 0;
        while (HE* he = hv_iternext(evidence)) {
            
            I32 len;
            self->names[i] = strdup(hv_iterkey(he, &len));
            
            SV *sv = hv_iterval(evidence, he);
            if ((self->bands[i] = SvBand(sv))) {
                SvREFCNT_inc(self->band_svs[i] = sv);
                
                int w = GDALGetRasterBandXSize(self->bands[i]),
                    h = GDALGetRasterBandYSize(self->bands[i]);
       
                if (self->width < 0) {
                    self->width = w;
                    self->height = h;
                } else {
                    if (w != self->width || h != self->height) {
                        croak("Band sizes differ. Expected (%i, %i), got (%i, %i).)",
                              self->width, self->height, w, h);
                    }
                }

                GDALDataType dt = GDALGetRasterDataType(self->bands[i]);
                if (!(dt == GDT_Byte ||
                      dt == GDT_UInt16 ||
                      dt == GDT_Int16 ||
                      dt == GDT_UInt32 ||
                      dt == GDT_Int32)) {
                    croak("Evidence band %s is not integer (it is %s).", self->names[i], GDALGetDataTypeName(dt));
                }
        
            } else {
                croak("Value in node %s is not a valid Geo::GDAL::Band object.", self->names[i]);
            }
            i += 1;
        }
    } else {
        destroy(self);
        croak("The setup must have an entry '%s', which points to a hashref.", key);
    }

    key = "offsets";
    if (HV *offsets = SvHash(hv_fetch(setup, key, strlen(key), 0))) {
        hv_iterinit(offsets);
        while (HE* he = hv_iternext(offsets)) {
            I32 len;
            char *name = hv_iterkey(he, &len);
            int found = 0;
            for (int i = 0; i < self->n; i++) {
                if (strcmp(self->names[i], name) == 0) {
                    self->offsets[i] = SvIV(hv_iterval(offsets, he));
                    found = 1;
                }
            }
            if (!found) {
                destroy(self);
                croak("Unknown node name in offsets: %s.", name);
            }
        }
    }

    key = "output";
    if (HV *output = SvHash(hv_fetch(setup, key, strlen(key), 0))) {
        int i = self->n-1;
        
        key = "name";
        SV **svp = hv_fetch(output, key, strlen(key), 0);
        if (svp) {                 
            self->names[i] = strdup(SvPV_nolen(*svp));
        } else {
            croak("output entry needs a %s entry.", key);
        }
        
        key = "band";
        svp = hv_fetch(output, key, strlen(key), 0);
        if (svp && (self->bands[i] = SvBand(*svp))) {
            SvREFCNT_inc(self->band_svs[i] = *svp);
            
            int w = GDALGetRasterBandXSize(self->bands[i]),
                h = GDALGetRasterBandYSize(self->bands[i]);
            
            if (self->width < 0) {
                self->width = w;
                self->height = h;
            } else {
                if (w != self->width || h != self->height) {
                    croak("Band sizes differ. Expected (%i, %i), got (%i, %i).)",
                          self->width, self->height, w, h);
                }
            }
            
            GDALDataType dt = GDALGetRasterDataType(self->bands[i]);
            if (!(dt == GDT_Float32 || dt == GDT_Float64)) {
                croak("Output band %s is not floating point (it is %s)", self->names[i], GDALGetDataTypeName(dt));
            }
        
        } else {
            croak("output entry needs a %s and entry which is a valid Geo::GDAL::Band object.", key);
        }

        key = "state";
        svp = hv_fetch(output, key, strlen(key), 0);
        if (svp) {                 
            self->value = (SvIV(*svp));
        } else {
            croak("output entry needs a %s entry.", key);
        }
        
    } else {
        destroy(self);
        croak("The setup must have an entry '%s', which points to a hashref.", key);
    }   

    key = "domain";
    SV **svp = hv_fetch(setup, key, strlen(key), 0);
    if (svp && SvROK(*svp) && sv_isobject(*svp) && sv_derived_from(*svp, "Hugin::Domain")) {
        IV tmp = SvIV((SV*)SvRV(*svp));
        self->domain = INT2PTR(Hugin_Domain, tmp);
        SvREFCNT_inc(self->domain_sv = *svp);
    } else {
        destroy(self);
        croak("The setup bands have a %s entry, which points to a valid Hugin::Domain object.", key);
    }
    
    return self;
}

void destroy(Geo_GDAL_Bayes_Hugin self) {
    if (self->names) {
        for (int i = 0; i < self->n; i++) {
            if (self->names[i]) free(self->names[i]);
        }
        free(self->names);
    }
    if (self->offsets) free(self->offsets);
    if (self->bands) {
        for (int i = 0; i < self->n; i++) {
            if (self->band_svs[i]) SvREFCNT_dec(self->band_svs[i]);
        }
        free(self->bands);
    }
    if (self->domain_sv) SvREFCNT_dec(self->domain_sv);
    free(self);
}

void compute(Geo_GDAL_Bayes_Hugin self) {

    int XBlockSize = 256, YBlockSize = 256;
    int XBlocks = (self->width + XBlockSize - 1) / XBlockSize;
    int YBlocks = (self->height + YBlockSize - 1) / YBlockSize;

    void **data = (void**)calloc(sizeof(void*), self->n);
    for (int i = 0; i < self->n; i++) {
        GDALDataType dt = GDALGetRasterDataType(self->bands[i]);
        data[i] = CPLMalloc(XBlockSize * YBlockSize * GDALGetDataTypeSizeBytes(dt));
    }

    for (int yb = 0; yb < YBlocks; ++yb) {
        for (int xb = 0; xb < XBlocks; ++xb) {
#ifdef DEBUG
            fprintf(stderr, "block: %i %i\n", xb, yb);
#endif
            int XValid = self->width % XBlockSize,
                YValid = self->height % YBlockSize;
#ifdef DEBUG
            fprintf(stderr, "block size: %i %i\n", XValid, YValid);
#endif
            
            for (int i = 0; i < self->n-1; i++) {
                CPLErr e = GDALRasterIO(
                    self->bands[i], GF_Read,
                    XBlockSize * xb, YBlockSize * yb,
                    XValid, YValid,
                    data[i],
                    XValid, YValid,
                    GDALGetRasterDataType(self->bands[i]),
                    0,0);
#ifdef DEBUG
                fprintf(stderr, "(read error = %i)", e);
#endif
            }
#ifdef DEBUG
            fprintf(stderr, "\n");
#endif
            
            for (int iY = 0; iY < YValid; ++iY) {
                for (int iX = 0; iX < XValid; ++iX) {
#ifdef DEBUG
                    fprintf(stderr, "%i %i: ", iX, iY);
#endif
                    
                    for (int i = 0; i < self->n-1; i++) {
                            
                        int32_t k = 0;
                        switch(GDALGetRasterDataType(self->bands[i])) {
                        case GDT_Byte:
                            k = ((GByte*)(data[i]))[iX + iY * XValid];
                            break;
                        case GDT_UInt16:
                            k = ((GUInt16*)(data[i]))[iX + iY * XValid];
                            break;
                        case GDT_Int16:
                            k = ((GInt16*)(data[i]))[iX + iY * XValid];
                            break;
                        case GDT_UInt32:
                            k = ((GUInt32*)(data[i]))[iX + iY * XValid];
                            break;
                        case GDT_Int32:
                            k = ((GInt32*)(data[i]))[iX + iY * XValid];
                            break;
                        }
                        int ok;
                        double no_data = GDALGetRasterNoDataValue(self->bands[i], &ok);
                        if (ok && k == no_data) {
                            continue;
                        }

#ifdef DEBUG
                        fprintf(stderr, "%s = %i ", self->names[i], k);
#endif
                        h_node_t node = h_domain_get_node_by_name(self->domain, self->names[i]);
                        k += self->offsets[i];
                        h_status_t e = h_node_select_state(node, k);
                        if (e) {
                            croak("Error in node_select_state: %s\n", h_error_description(e));
                        }
                        
                    }

                    h_status_t e = h_domain_propagate(self->domain, h_equilibrium_sum, h_mode_normal);
                    if (e) {
                        croak("Error in domain_propagate: %s\n", h_error_description(e));
                    }
                    
                    int i = self->n-1;

                    h_node_t node = h_domain_get_node_by_name(self->domain, self->names[i]);
                    double nv = h_node_get_belief(node, self->value);
#ifdef DEBUG
                    fprintf(stderr, "%s = %f\n", self->names[i], nv);
#endif
                            
                    switch(GDALGetRasterDataType(self->bands[i])) {
                    case GDT_Float32:
                        ((float*)(data[i]))[iX + iY * XValid] = nv;
                        break;
                    case GDT_Float64:
                        ((double*)(data[i]))[iX + iY * XValid] = nv;
                        break;
                    }
                }
            }

            int i = self->n-1;
            CPLErr e = GDALRasterIO(
                self->bands[i], GF_Write,
                XBlockSize * xb, YBlockSize * yb,
                XValid, YValid,
                data[i],
                XValid, YValid,
                GDALGetRasterDataType(self->bands[i]),
                0,0);
#ifdef DEBUG
            fprintf(stderr, "(write error = %i)", e);
#endif
        }
    }
    
    for (int i = 0; i < self->n; i++) {
        free(data[i]);
    }
    free(data);
}
