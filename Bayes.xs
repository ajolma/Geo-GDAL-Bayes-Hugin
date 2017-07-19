#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#undef seed
#undef do_open
#undef do_close

#include "ppport.h"

#include <bayes.h>

#include "const-c.inc"

MODULE = Geo::GDAL::Bayes::Hugin        PACKAGE = Geo::GDAL::Bayes::Hugin

INCLUDE: const-xs.inc

Geo_GDAL_Bayes_Hugin new(SV *klass, HV *setup)
        CODE:
		RETVAL = create(setup);
	OUTPUT:
		RETVAL

void DESTROY(Geo_GDAL_Bayes_Hugin self)
        CODE:
        destroy(self);

void compute(Geo_GDAL_Bayes_Hugin self)
        CODE:
        compute(self);
        
