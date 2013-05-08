/* msr_rapl.c
 */

#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <stddef.h>
#include "msr_common.h"
#include "msr_core.h"
#include "msr_rapl.h"

// Section 35.7
// Table 35-11.  MSRs supported by Intel processors based on Intel 
// microarchitecture code name Sandy Bridge.
// Model/family 06_2A, 06_2D.
#if (USE_062A || USE_062D)
#define MSR_RAPL_POWER_UNIT 		(0x606)	// ro
#define MSR_PKG_POWER_LIMIT 		(0x610) // rw
#define MSR_PKG_ENERGY_STATUS 		(0x611) // ro sic;  MSR_PKG_ENERY_STATUS
#define MSR_PKG_POWER_INFO 		(0x614) // rw (text states ro)
#define MSR_PP0_POWER_LIMIT 		(0x638) // rw
#define MSR_PP0_ENERY_STATUS 		(0x639) // ro
#define MSR_PP0_POLICY 			(0x63A) // rw
#define MSR_PP0_PERF_STATUS 		(0x63B) // ro
#endif

// Section 35.7.1
// Table 35-12. MSRs supported by second generation Intel Core processors 
// (Intel microarchitecture Code Name Sandy Bridge)
// Model/family 06_2AH
#if (USE_062A)
#define MSR_PP1_POWER_LIMIT 		(0x640) // rw
#define MSR_PP1_ENERGY_STATUS 		(0x641)	// ro.  sic; MSR_PP1_ENERY_STATUS
#define MSR_PP1_POLICY 			(0x642) // rw
#endif

// Section 35.7.2
// Table 35-13. Selected MSRs supported by Intel Xeon processors E5 Family 
// (based on Intel Microarchitecture code name Sandy Bridge) 
// Model/family 06_2DH
#if (USE_062D)
#define MSR_PKG_PERF_STATUS 		(0x613) // ro
#define MSR_DRAM_POWER_LIMIT 		(0x618) // rw	
#define MSR_DRAM_ENERGY_STATUS 		(0x619)	// ro.  sic; MSR_DRAM_ENERY_STATUS
#define MSR_DRAM_PERF_STATUS 		(0x61B) // ro
#define MSR_DRAM_POWER_INFO 		(0x61C) // rw (text states ro)
#endif

// Section 35.8.1
// Table 35-15. Selected MSRs supported by Intel Xeon processors E5 Family v2 
// (based on Intel microarchitecture code name Ivy Bridge) 
// Model/family 06_3EH.  
// The Intel documentation only lists this table of msrs; this may be an error.
#if (USE_063E)
#define MSR_PKG_PERF_STATUS 		(0x613) //
#define MSR_DRAM_POWER_LIMIT 		(0x618) //
#define MSR_DRAM_ENERGY_STATUS 		(0x619)	// sic; MSR_DRAM_ENERY_STATUS
#define MSR_DRAM_PERF_STATUS 		(0x61B) //
#define MSR_DRAM_POWER_INFO 		(0x61C) //
#endif

enum{
	BITS_TO_WATTS,
	WATTS_TO_BITS,
	BITS_TO_SECONDS,
	SECONDS_TO_BITS,
	BITS_TO_JOULES,
	JOULES_TO_BITS,
	NUM_XLATE
};

struct rapl_units{
	uint64_t msr_rapl_power_unit;	// raw msr value
	double seconds;
	double joules;
	double watts;
};

static void
translate( int package, uint64_t* bits, double* units, int type ){
	static int initialized=0;
	static struct rapl_units ru[NUM_PACKAGES];
	int i;
	if(!initialized){
		initialized=1;
		for(i=0; i<NUM_PACKAGES; i++){
			// See figure 14-16 for bit fields.
			//  1  1 1  1 1 
			//  9  6 5  2 1  8 7  4 3  0
			//
			//  1010 0001 0000 0000 0011
			//
			//     A    1    0    0    3
			//ru[i].msr_rapl_power_unit = 0xA1003;

			read_msr( i, MSR_RAPL_POWER_UNIT, &(ru[i].msr_rapl_power_unit) );
			// default is 1010b or 976 microseconds
			ru[i].seconds = 1.0/(double)( 1<<(MASK_VAL( ru[i].msr_rapl_power_unit, 19, 16 )));
			// default is 10000b or 15.3 microjoules
			ru[i].joules  = 1.0/(double)( 1<<(MASK_VAL( ru[i].msr_rapl_power_unit, 12,  8 )));
			// default is 0011b or 1/8 Watts
			ru[i].watts   = ((1.0)/((double)( 1<<(MASK_VAL( ru[i].msr_rapl_power_unit,  3,  0 )))));
		}	
	}
	if(type == WATTS_TO_BITS){
		fprintf(stdout, "%s:%d watts = %lf\n", __FILE__, __LINE__, *units);
		fprintf(stdout, "%s:%d ru[package].watts = %lf\n", __FILE__, __LINE__, ru[package].watts);
		fprintf(stdout, "%s:%d dividend is %lf\n", __FILE__, __LINE__, *units / ru[package].watts);
		fprintf(stdout, "%s:%d dividend cast to uint64_t is 0x%lx\n", __FILE__, __LINE__, (uint64_t)(*units / ru[package].watts));
	}
	switch(type){
		case BITS_TO_WATTS: 	*units = (double)(*bits)  * ru[package].watts; 		break;
		case BITS_TO_SECONDS:	*units = (double)(*bits)  * ru[package].seconds; 	break;
		case BITS_TO_JOULES:	*units = (double)(*bits)  * ru[package].joules; 	break;
		case WATTS_TO_BITS:	*bits  = (uint64_t)(  (*units) / ru[package].watts    ); 	break;
		case SECONDS_TO_BITS:	*bits  = (uint64_t)(  (*units) / ru[package].seconds  ); 	break;
		case JOULES_TO_BITS:	*bits  = (uint64_t)(  (*units) / ru[package].joules   ); 	break;
		default: 
			fprintf(stderr, "%s:%d  Unknown value %d.  This is bad.\n", __FILE__, __LINE__, type);  
			*bits = -1;
			*units= -1.0;
			break;
	}
}

struct rapl_power_info{
	uint64_t msr_pkg_power_info;	// raw msr values
	uint64_t msr_dram_power_info;

	double pkg_max_power;		// watts
	double pkg_min_power;
	double pkg_max_window;		// seconds
	double pkg_therm_power;		// watts

	double dram_max_power;		// watts
	double dram_min_power;
	double dram_max_window;		// seconds
	double dram_therm_power;	// watts
};

static void
rapl_get_power_info(int package, struct rapl_power_info *info){
	uint64_t val = 0;
	//info->msr_pkg_power_info  = 0x6845000148398;
	//info->msr_dram_power_info = 0x682d0001482d0;

	read_msr( package, MSR_PKG_POWER_INFO, &(info->msr_pkg_power_info) );
	read_msr( package, MSR_DRAM_POWER_INFO, &(info->msr_dram_power_info) );

	// Note that the same units are used in both the PKG and DRAM domains.
	// Also note that "package", "socket" and "cpu" are being used interchangably.  This needs to be fixed.
	
	val = MASK_VAL( info->msr_pkg_power_info,  53, 48 );
	translate( package, &val, &(info->pkg_max_window), BITS_TO_SECONDS );
	
	val = MASK_VAL( info->msr_pkg_power_info,  46, 32 );
	translate( package, &val, &(info->pkg_max_power), BITS_TO_WATTS );

	val = MASK_VAL( info->msr_pkg_power_info,  30, 16 );
	translate( package, &val, &(info->pkg_min_power), BITS_TO_WATTS );

	val = MASK_VAL( info->msr_pkg_power_info,  14,  0 );
	translate( package, &val, &(info->pkg_therm_power), BITS_TO_WATTS );

	val = MASK_VAL( info->msr_dram_power_info, 53, 48 );
	translate( package, &val, &(info->dram_max_window), BITS_TO_SECONDS );

	val = MASK_VAL( info->msr_dram_power_info, 46, 32 );
	translate( package, &val, &(info->dram_max_power), BITS_TO_WATTS );

	val = MASK_VAL( info->msr_dram_power_info, 30, 16 );
	translate( package, &val, &(info->dram_min_power), BITS_TO_WATTS );

	val = MASK_VAL( info->msr_dram_power_info, 14,  0 );
	translate( package, &val, &(info->dram_therm_power), BITS_TO_WATTS );
}

static void
rapl_limit_calc(int package, struct rapl_limit* limit1, struct rapl_limit* limit2, struct rapl_limit* dram ){
	static struct rapl_power_info rpi[NUM_PACKAGES];
	static int initialized=0;
	uint64_t watts_bits=0, seconds_bits=0;
	int i;
	if(!initialized){
		initialized=1;
		for(i=0; i<NUM_PACKAGES; i++){
			rapl_get_power_info(i, &(rpi[i]));
		}
	}
	if(limit1){
		if (limit1->bits){
			// We have been given the bits to be written to the msr.
			// For sake of completeness, translate these into watts 
			// and seconds.
			watts_bits   = MASK_VAL( limit1->bits, 14,  0 );
			seconds_bits = MASK_VAL( limit1->bits, 23, 17 );

			translate( package, &watts_bits, &limit1->watts, BITS_TO_WATTS );
			translate( package, &seconds_bits, &limit1->seconds, BITS_TO_SECONDS );

		}else{
			// We have been given watts and seconds and need to translate
			// these into bit values.
			translate( package, &watts_bits,   &limit1->watts,   WATTS_TO_BITS   );
			translate( package, &seconds_bits, &limit1->seconds, SECONDS_TO_BITS );
			limit1->bits |= watts_bits   << 0;
			limit1->bits |= seconds_bits << 17;
		}
	}	
	if(limit2){
		if (limit2->bits){
			watts_bits   = MASK_VAL( limit2->bits, 46, 32 );
			seconds_bits = MASK_VAL( limit2->bits, 55, 49 );

			translate( package, &watts_bits, &limit2->watts, BITS_TO_WATTS );
			translate( package, &seconds_bits, &limit2->seconds, BITS_TO_SECONDS );

		}else{
			translate( package, &watts_bits,   &limit2->watts,   WATTS_TO_BITS   );
			translate( package, &seconds_bits, &limit2->seconds, SECONDS_TO_BITS );
			limit2->bits |= watts_bits   << 32;
			limit2->bits |= seconds_bits << 49;
		}
	}
	if(dram){
		if (dram->bits){
			// We have been given the bits to be written to the msr.
			// For sake of completeness, translate these into watts 
			// and seconds.
			watts_bits   = MASK_VAL( dram->bits, 14,  0 );
			seconds_bits = MASK_VAL( dram->bits, 23, 17 );

			translate( package, &watts_bits, &dram->watts, BITS_TO_WATTS );
			translate( package, &seconds_bits, &dram->seconds, BITS_TO_SECONDS );

		}else{
			// We have been given watts and seconds and need to translate
			// these into bit values.
			translate( package, &watts_bits,   &dram->watts,   WATTS_TO_BITS   );
			translate( package, &seconds_bits, &dram->seconds, SECONDS_TO_BITS );
			dram->bits |= watts_bits   << 0;
			dram->bits |= seconds_bits << 17;
		}
	}
}

void
rapl_dump_limit( struct rapl_limit* L ){
	fprintf(stdout, "bits    = %lx\n", L->bits);
	fprintf(stdout, "seconds = %lf\n", L->seconds);
	fprintf(stdout, "watts   = %lf\n", L->watts);
	fprintf(stdout, "\n");
}

void 
rapl_set_limit( int package, struct rapl_limit* limit1, struct rapl_limit* limit2, struct rapl_limit* dram ){
	// Fill in whatever values are necessary.
	uint64_t pkg_limit=0;
	uint64_t dram_limit=0;
	rapl_limit_calc( package, limit1, limit2, dram );

	if(limit1){
		pkg_limit |= limit1->bits | (1LL << 15) | (1LL << 16);	// enable clamping
	}
	if(limit2){
		pkg_limit |= limit2->bits | (1LL << 47) | (1LL << 48);	// enable clamping
	}
	if(limit1 || limit2){
		write_msr( package, MSR_PKG_POWER_LIMIT, pkg_limit );
	}
	if(dram){
		dram_limit |= dram->bits | (1LL << 15) | (1LL << 16);	// enable clamping
		write_msr( package, MSR_DRAM_POWER_LIMIT, dram_limit );
	}
}

void 
rapl_get_limit( int package, struct rapl_limit* limit1, struct rapl_limit* limit2, struct rapl_limit* dram ){
	if(limit1){
		read_msr( package, MSR_PKG_POWER_LIMIT, &(limit1->bits) );
	}
	if(limit2){
		read_msr( package, MSR_PKG_POWER_LIMIT, &(limit2->bits) );
	}
	if(dram){
		read_msr( package, MSR_DRAM_POWER_LIMIT, &(dram->bits) );
	}
	// Fill in whatever values are necessary.
	rapl_limit_calc( package, limit1, limit2, dram );
}

void 
rapl_dump_data( struct rapl_data *r ){
	fprintf(stdout, "%lf %lf %lf %lf %lf\n", 
			r->pkg_joules,
			r->dram_joules,
			r->pkg_watts,
			r->dram_watts,
			r->elapsed);
}

void
rapl_read_data( int package, struct rapl_data *r ){
	uint64_t pkg_bits, dram_bits;
	static struct timeval start[NUM_PACKAGES];
	static struct timeval stop[NUM_PACKAGES];

	// Copy previous stop to start.
	start[package].tv_sec = stop[package].tv_sec;
	start[package].tv_usec = stop[package].tv_usec;

	// Get current timestamp
	gettimeofday( &(stop[package]), NULL );

	if(r){
		// Get delta in seconds
		r->elapsed = (stop[package].tv_sec - start[package].tv_sec) 
			     +
			     (stop[package].tv_usec - start[package].tv_usec)/1000000.0;

		// Get raw joules
		read_msr( package, MSR_PKG_ENERGY_STATUS, &pkg_bits );
		read_msr( package, MSR_DRAM_ENERGY_STATUS, &dram_bits );

		// get normalized joules
		translate( package, &pkg_bits, &(r->pkg_joules), BITS_TO_JOULES );
		translate( package, &dram_bits, &(r->dram_joules), BITS_TO_JOULES );

		// record normalized joules
		r->pkg_watts = r->pkg_joules / r->elapsed;
		r->dram_watts = r->dram_joules / r->elapsed;
	}
}


