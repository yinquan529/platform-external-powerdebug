/*******************************************************************************
 * Copyright (C) 2010, Linaro
 * Copyright (C) 2010, IBM Corporation
 *
 * This file is part of PowerDebug.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Amit Arora <amit.arora@linaro.org> (IBM Corporation)
 *       - initial API and implementation
 *******************************************************************************/

#include "powerdebug.h"
#include "sensor.h"

char *get_num(char *fname, char *sensor)
{
	char tmpstr[NAME_MAX];
	char *str;

	strcpy(tmpstr, (fname+strlen(sensor)));

	str = strrchr(tmpstr, '_');
	str[0] = '\0';

	str = strdup(tmpstr);
	return str;
}
	

void get_sensor_info(char *path, char *fname, char *sensor, int verbose)
{
	FILE *filep;
	char filename[PATH_MAX];
	char **items = NULL, **suffix = NULL;
	char *item, result[NAME_MAX], *num;
	int ret, count = 0;

	(void)verbose; // get rid of warning

	sprintf(filename, "%s/%s", path, fname);

	if(!strcmp(sensor, "in")) {
		items = (char **)items_in;
		suffix = (char **)suffix_in;
	}

	if(!strcmp(sensor, "temp")) {
		items = (char **)items_temp;
		suffix = (char **)suffix_temp;
	}

	if(!strcmp(sensor, "fan")) {
		items = (char **)items_fan;
		suffix = (char **)suffix_fan;
	}
	if(!strcmp(sensor, "pwm")) {
		items = (char **)items_pwm;
		suffix = (char **)suffix_pwm;
	}


	if (!items || !suffix)
		return;

	item = strrchr(fname, '_');
	if(!item)
		return;

	if(item)
		item++;

	if(item > (fname + strlen(fname)))
		return;

	num = get_num(fname, sensor);
	filep = fopen(filename, "r");

	if(!filep) 
		goto exit;
	ret = fscanf(filep, "%s", result);
	fclose(filep);

	if(ret != 1)
		goto exit;

	while(strcmp(items[count], "")) {
		if(!strcmp(items[count], item))
			printf("\'temp\' %s sensor %s\t\t%d%s\n",
				num, items[count], atoi(result)/1000,
				suffix[count]);
		count++;
	}
exit:
	free(num);
	return;
}
