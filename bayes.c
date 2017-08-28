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

h_node_t get_node_by_label_or_name(h_domain_t domain, const char *label) {
    for (h_node_t node = h_domain_get_first_node(domain); node; node = h_node_get_next(node)) {
        if (strcmp(h_node_get_label(node), label) == 0) {
            return node;
        }
    }
    for (h_node_t node = h_domain_get_first_node(domain); node; node = h_node_get_next(node)) {
        if (strcmp(h_node_get_name(node), label) == 0) {
            return node;
        }
    }
    return NULL;
}

Geo_GDAL_Bayes_Hugin create(HV *setup) {

    Geo_GDAL_Bayes_Hugin self = (Geo_GDAL_Bayes_Hugin)malloc(sizeof(Geo_GDAL_Bayes_Hugin_t));
    self->n_evidence_nodes = 0;
    self->evidence_nodes = NULL;
    self->evidence_offsets = NULL;
    self->evidence_bands = NULL;
    self->evidence_band_svs = NULL;
    self->output_node = NULL;
    self->output_from_state = -1;
    self->output_band = NULL;
    self->output_band_sv = NULL;
    self->domain = NULL;
    self->domain_sv = NULL;

    const char *key = "domain";
    SV **svp = hv_fetch(setup, key, strlen(key), 0);
    if (svp && SvROK(*svp) && sv_isobject(*svp) && sv_derived_from(*svp, "Hugin::Domain")) {
        IV tmp = SvIV((SV*)SvRV(*svp));
        self->domain = INT2PTR(Hugin_Domain, tmp);
        SvREFCNT_inc(self->domain_sv = *svp);
    } else {
        destroy(self);
        croak("Missing %s => $domain or the $domain is not a valid Hugin::Domain object.", key);
    }

    // output is required, evidence is not
    key = "output";
    if (HV *output = SvHash(hv_fetch(setup, key, strlen(key), 0))) {

        key = "node";
        SV **svp = hv_fetch(output, key, strlen(key), 0);
        if (svp) {
            self->output_node = get_node_by_label_or_name(self->domain, SvPV_nolen(*svp));
            if (!self->output_node) {
                croak("Node '%s' does not exist.", SvPV_nolen(*svp));
            }
        } else {
            croak("Missing output => {%s => $node_label}.", key);
        }

        key = "state";
        svp = hv_fetch(output, key, strlen(key), 0);
        if (svp) {
            const char *state = SvPV_nolen(*svp);
            for (int i = 0; i < h_node_get_number_of_states(self->output_node); i++) {
                if (strcmp(h_node_get_state_label(self->output_node, i), state) == 0) {
                    self->output_from_state = i;
                }
            }
            if (self->output_from_state < 0) {
                croak("Node '%s' does not have state '%s'.", h_node_get_name(self->output_node), state);
            }
        } else {
            croak("Missing output => {%s => $state}.", key);
        }

        key = "band";
        svp = hv_fetch(output, key, strlen(key), 0);
        if (svp && (self->output_band = SvBand(*svp))) {
            SvREFCNT_inc(self->output_band_sv = *svp);

            self->width = GDALGetRasterBandXSize(self->output_band);
            self->height = GDALGetRasterBandYSize(self->output_band);

            GDALDataType dt = GDALGetRasterDataType(self->output_band);
            if (!(dt == GDT_Float32 || dt == GDT_Float64)) {
                croak("Output band %s is not floating point (it is %s)", self->output_node, GDALGetDataTypeName(dt));
            }

        } else {
            croak("Missing output => {%s => $band} or the $band is not a valid Geo::GDAL::Band.", key);
        }

    } else {
        destroy(self);
        croak("Missing %s => {}.", key);
    }

    key = "evidence";
    if (HV *evidence = SvHash(hv_fetch(setup, key, strlen(key), 0))) {
        hv_iterinit(evidence);
        self->n_evidence_nodes = 0;
        while (HE* he = hv_iternext(evidence)) {
            self->n_evidence_nodes += 1;
        }
        self->evidence_nodes = (h_node_t *)calloc(sizeof(h_node_t), self->n_evidence_nodes);
        self->evidence_offsets = (int *)calloc(sizeof(int), self->n_evidence_nodes);
        self->evidence_bands = (GDALRasterBandH *)calloc(sizeof(GDALRasterBandH*), self->n_evidence_nodes);
        self->evidence_band_svs = (SV **)calloc(sizeof(SV*), self->n_evidence_nodes);
        hv_iterinit(evidence);
        int i = 0;
        while (HE* he = hv_iternext(evidence)) {

            I32 len;
            self->evidence_nodes[i] = get_node_by_label_or_name(self->domain, hv_iterkey(he, &len));

            SV *sv = hv_iterval(evidence, he);
            if ((self->evidence_bands[i] = SvBand(sv))) {
                SvREFCNT_inc(self->evidence_band_svs[i] = sv);

                int w = GDALGetRasterBandXSize(self->evidence_bands[i]),
                    h = GDALGetRasterBandYSize(self->evidence_bands[i]);

                if (w != self->width || h != self->height) {
                    croak("Band sizes differ. Expected (%i, %i), got (%i, %i) in %s.)",
                          self->width, self->height, w, h, self->evidence_nodes[i]);
                }

                GDALDataType dt = GDALGetRasterDataType(self->evidence_bands[i]);
                if (!(dt == GDT_Byte ||
                      dt == GDT_UInt16 ||
                      dt == GDT_Int16 ||
                      dt == GDT_UInt32 ||
                      dt == GDT_Int32)) {
                    croak("Evidence band %s is not integer (it is %s).", self->evidence_nodes[i], GDALGetDataTypeName(dt));
                }

            } else {
                croak("Value in node %s is not a valid Geo::GDAL::Band object.", self->evidence_nodes[i]);
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
            I32 offset = SvIV(hv_iterval(offsets, he));
            for (int i = 0; i < self->n_evidence_nodes; i++) {
                if (strcmp(h_node_get_label(self->evidence_nodes[i]), name) == 0) {
                    self->evidence_offsets[i] = offset;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                destroy(self);
                croak("Unknown node name in offsets: %s.", name);
            }
        }
    }

    return self;
}

void destroy(Geo_GDAL_Bayes_Hugin self) {
    if (self->evidence_nodes) {
        free(self->evidence_nodes);
    }
    if (self->evidence_offsets) free(self->evidence_offsets);
    if (self->evidence_bands) {
        for (int i = 0; i < self->n_evidence_nodes; i++) {
            if (self->evidence_band_svs[i]) SvREFCNT_dec(self->evidence_band_svs[i]);
        }
        free(self->evidence_band_svs);
        free(self->evidence_bands);
    }
    if (self->output_band_sv) SvREFCNT_dec(self->output_band_sv);
    if (self->domain_sv) SvREFCNT_dec(self->domain_sv);
    free(self);
}

void compute(Geo_GDAL_Bayes_Hugin self) {

    int XBlockSize = 256, YBlockSize = 256;
    int XBlocks = (self->width + XBlockSize - 1) / XBlockSize;
    int YBlocks = (self->height + YBlockSize - 1) / YBlockSize;

    void **data = (void**)calloc(sizeof(void*), self->n_evidence_nodes);
    for (int i = 0; i < self->n_evidence_nodes; i++) {
        GDALDataType dt = GDALGetRasterDataType(self->evidence_bands[i]);
        data[i] = CPLMalloc(XBlockSize * YBlockSize * GDALGetDataTypeSizeBytes(dt));
    }
    GDALDataType dt = GDALGetRasterDataType(self->output_band);
    void *output_data = CPLMalloc(XBlockSize * YBlockSize * GDALGetDataTypeSizeBytes(dt));

    for (int yb = 0; yb < YBlocks; ++yb) {
        for (int xb = 0; xb < XBlocks; ++xb) {

            int XPixelOff = xb * XBlockSize;
            int YPixelOff = yb * YBlockSize;

            int XValid = XBlockSize;
            int YValid = YBlockSize;

            if (XPixelOff + XBlockSize >= self->width) {
                XValid = self->width - XPixelOff;
            }

            if (YPixelOff + YBlockSize >= self->height) {
                YValid = self->height - YPixelOff;
            }

            for (int i = 0; i < self->n_evidence_nodes; i++) {
                CPLErr e = GDALRasterIO(
                    self->evidence_bands[i], GF_Read,
                    XBlockSize * xb, YBlockSize * yb,
                    XValid, YValid,
                    data[i],
                    XValid, YValid,
                    GDALGetRasterDataType(self->evidence_bands[i]),
                    0,0);
                if (e) {
                    croak("Error (%i) while reading a block of data for %s.", e, self->evidence_nodes[i]);
                }
            }

            for (int iY = 0; iY < YValid; ++iY) {
                for (int iX = 0; iX < XValid; ++iX) {

                    for (int i = 0; i < self->n_evidence_nodes; i++) {

                        int32_t k = 0;
                        switch(GDALGetRasterDataType(self->evidence_bands[i])) {
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
                        double no_data = GDALGetRasterNoDataValue(self->evidence_bands[i], &ok);
                        if (ok && k == no_data) {
                            continue;
                        }

                        k += self->evidence_offsets[i];
                        h_status_t e = h_node_select_state(self->evidence_nodes[i], k);
                        if (e) {
                            croak("Error in node_select_state: %s\n", h_error_description(e));
                        }

                    }

                    h_status_t e = h_domain_propagate(self->domain, h_equilibrium_sum, h_mode_normal);
                    if (e) {
                        croak("Error in domain_propagate: %s\n", h_error_description(e));
                    }

                    double nv = h_node_get_belief(self->output_node, self->output_from_state);

                    switch(GDALGetRasterDataType(self->output_band)) {
                    case GDT_Float32:
                        ((float*)(output_data))[iX + iY * XValid] = nv;
                        break;
                    case GDT_Float64:
                        ((double*)(output_data))[iX + iY * XValid] = nv;
                        break;
                    }
                }
            }

            CPLErr e = GDALRasterIO(
                self->output_band, GF_Write,
                XBlockSize * xb, YBlockSize * yb,
                XValid, YValid,
                output_data,
                XValid, YValid,
                GDALGetRasterDataType(self->output_band),
                0,0);
            if (e) {
                croak("Error (%i) while writing a block of data from %s.", e, self->output_node);
            }
        }
    }

    for (int i = 0; i < self->n_evidence_nodes; i++) {
        free(data[i]);
    }
    free(data);
    free(output_data);
}
