/*
 * Copyright (c) 2006 Luc Verhaegen (quirks list)
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * DDC probing routines (drm_ddc_read & drm_do_probe_ddc_edid) originally from
 * FB layer.
 *   Copyright (C) 2006 Dennis Munsie <dmunsie@cecropia.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* edited to fit in libxenon */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time/time.h>
#include <xenon_smc/xenon_smc.h>
#include <debug.h>
#include "xenos_edid.h"

/* define the number of Extension EDID block */
#define MAX_EDID_EXT_NUM 4

/* Valid EDID header has these bytes */
static const u8 edid_header[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};

/**
 * edid_is_valid - sanity check EDID data
 * @edid: EDID data
 *
 * Sanity check the EDID block by looking at the header, the version number
 * and the checksum.  Return 0 if the EDID doesn't check out, or 1 if it's
 * valid.
 */
static BOOL edid_is_valid(struct edid *edid)
{
	int i, score = 0;
	u8 csum = 0;
	u8 *raw_edid = (u8 *)edid;

	for (i = 0; i < sizeof(edid_header); i++)
		if (raw_edid[i] == edid_header[i])
			score++;

	if (score == 8) ;
	else if (score >= 6) {
		printf("Fixing EDID header\n");
		memcpy(raw_edid, edid_header, sizeof(edid_header));
	} else
		goto bad;

	for (i = 0; i < EDID_LENGTH; i++)
		csum += raw_edid[i];
	if (csum) {
		printf("EDID checksum is invalid, remainder is %d\n", csum);
		goto bad;
	}

	if (edid->version != 1) {
		printf("EDID has major version %d, instead of 1\n", edid->version);
		goto bad;
	}

	if (edid->revision > 4)
		printf("EDID minor > 4, assuming backward compatibility\n");

	return 1;

bad:
	return 0;
}

static int ddc_init(){
	int ret=0;
	
	xenon_smc_i2c_ddc_lock(1);

	ret=xenon_smc_i2c_write(0x1ec, 0);
	if(ret) goto err;

	// address
	ret=xenon_smc_i2c_write(0x1ed, 0xa0);
	if(ret) goto err;	
		
	// 1 byte at a time
	ret=xenon_smc_i2c_write(0x1f0, 1); 
	if(ret) goto err;	
	ret=xenon_smc_i2c_write(0x1f1, 0);
	if(ret) goto err;	
	ret=xenon_smc_i2c_write(0x1f5, 1);
	if(ret) goto err;	
	
	// reset
	ret=xenon_smc_i2c_write(0x1f3, 0xf);
	if(ret) goto err;	
	ret=xenon_smc_i2c_write(0x1f3, 0xa);
	if(ret) goto err;	
	ret=xenon_smc_i2c_write(0x1f3, 9);
	if(ret) goto err;	
	ret=xenon_smc_i2c_write(0x1f2, 0x60);

err:
	xenon_smc_i2c_ddc_lock(0);
	return ret;
}

static int ddc_read_byte(int offset,unsigned char *b){
	int ret=0;
	unsigned char bb;
	
	xenon_smc_i2c_ddc_lock(1);
	
	// offset
	ret=xenon_smc_i2c_write(0x1ee, offset>>8);
	if(ret) goto err;	
	ret=xenon_smc_i2c_write(0x1ef, offset&0xff);
	if(ret) goto err;	
	
	// start
	ret=xenon_smc_i2c_write(0x1f3, 4);
	if(ret) goto err;	
	
	// wait for end
	for(;;){
		ret=xenon_smc_i2c_read(0x1f2, &bb);
		if(ret) goto err;	
		if(!(bb&0x10)) break;
		udelay(10);
	}
	
	// read result
	ret=xenon_smc_i2c_read(0x1f4, b);
	if(ret) goto err;	
	
err:
	xenon_smc_i2c_ddc_lock(0);
	return ret;
}


/**
 * Get EDID information via I2C.
 *
 * \param adapter : i2c device adaptor
 * \param buf     : EDID data buffer to be filled
 * \param len     : EDID data buffer length
 * \return 0 on success or -1 on failure.
 *
 * Try to fetch EDID information by calling i2c driver function.
 */
int xenos_do_probe_ddc_edid(unsigned char *buf, int len)
{
	int i;
	
	if(ddc_init()) return -1;
	
	for(i=0;i<len;++i) if (ddc_read_byte(i,&buf[i])) return -1;
	
//	buffer_dump(buf,len);
	return 0;
}

static int xenos_ddc_read_edid(unsigned char *buf, int len)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (xenos_do_probe_ddc_edid(buf, len))
			return -1;
		if (edid_is_valid((struct edid *)buf))
			return 0;
	}

	/* repeated checksum failures; warn, but carry on */
	printf("EDID invalid.\n");
	return -1;
}

/**
 * drm_get_edid - get EDID data, if available
 * @connector: connector we're probing
 * @adapter: i2c adapter to use for DDC
 *
 * Poke the given connector's i2c channel to grab EDID data if possible.
 *
 * Return edid data or NULL if we couldn't find any.
 */
struct edid *xenos_get_edid()
{
	int ret;
	struct edid *edid;

	edid = malloc(EDID_LENGTH * (MAX_EDID_EXT_NUM + 1));
	if (edid == NULL) {
		printf("Failed to allocate EDID\n");
		goto end;
	}

	/* Read first EDID block */
	ret = xenos_ddc_read_edid((unsigned char *)edid, EDID_LENGTH);
	if (ret != 0)
		goto clean_up;

	/* There are EDID extensions to be read */
	if (edid->extensions != 0) {
		int edid_ext_num = edid->extensions;

		if (edid_ext_num > MAX_EDID_EXT_NUM) {
			printf(
				 "The number of extension(%d) is "
				 "over max (%d), actually read number (%d)\n",
				 edid_ext_num, MAX_EDID_EXT_NUM,
				 MAX_EDID_EXT_NUM);
			/* Reset EDID extension number to be read */
			edid_ext_num = MAX_EDID_EXT_NUM;
		}
		/* Read EDID including extensions too */
		ret = xenos_ddc_read_edid((unsigned char *)edid, EDID_LENGTH * (edid_ext_num + 1));
		if (ret != 0)
			goto clean_up;

	}

	goto end;

clean_up:
	free(edid);
	edid = NULL;
end:
	return edid;

}

#define HDMI_IDENTIFIER 0x000C03
#define VENDOR_BLOCK    0x03
/**
 * drm_detect_hdmi_monitor - detect whether monitor is hdmi.
 * @edid: monitor EDID information
 *
 * Parse the CEA extension according to CEA-861-B.
 * Return true if HDMI, false if not or unknown.
 */
BOOL xenos_detect_hdmi_monitor(struct edid *edid)
{
	char *edid_ext = NULL;
	int i, hdmi_id, edid_ext_num;
	int start_offset, end_offset;
	BOOL is_hdmi = FALSE;

	/* No EDID or EDID extensions */
	if (edid == NULL || edid->extensions == 0)
		goto end;

	/* Chose real EDID extension number */
	edid_ext_num = edid->extensions > MAX_EDID_EXT_NUM ?
		       MAX_EDID_EXT_NUM : edid->extensions;

	/* Find CEA extension */
	for (i = 0; i < edid_ext_num; i++) {
		edid_ext = (char *)edid + EDID_LENGTH * (i + 1);
		/* This block is CEA extension */
		if (edid_ext[0] == 0x02)
			break;
	}

	if (i == edid_ext_num)
		goto end;

	/* Data block offset in CEA extension block */
	start_offset = 4;
	end_offset = edid_ext[2];

	/*
	 * Because HDMI identifier is in Vendor Specific Block,
	 * search it from all data blocks of CEA extension.
	 */
	for (i = start_offset; i < end_offset;
		/* Increased by data block len */
		i += ((edid_ext[i] & 0x1f) + 1)) {
		/* Find vendor specific block */
		if ((edid_ext[i] >> 5) == VENDOR_BLOCK) {
			hdmi_id = edid_ext[i + 1] | (edid_ext[i + 2] << 8) |
				  edid_ext[i + 3] << 16;
			/* Find HDMI identifier */
			if (hdmi_id == HDMI_IDENTIFIER)
				is_hdmi = TRUE;
			break;
		}
	}

end:
	return is_hdmi;
}