/**********************************************************************************************
	$Id: export.c,v 1.29 2006/01/18 22:45:51 bboy Exp $

	Outputs zone data in various formats.

	Copyright (C) 2002-2005  Don Moore <bboy@bboy.net>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at Your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**********************************************************************************************/

#include "named.h"
#include "util.h"

#include "memoryman.h"

#include "debug.h"
#include "export.h"
#include "rr.h"

#define DEFINE_RR_MAP 1

#define DEFINE_RR_TYPE(__TYPENAME__, __PERSISTENT__, __PARSER__, __FREE__, __DUPLICATOR__, \
		       __SIZOR__, __REPLY_ADD__, __UPDATE_GET_RR_DATA__, __PROCESS_AXFR__, \
		       __CHECK_RR__, \
		       __EXPORT_BIND_RR__, __EXPORT_TINYDNS_RR__, \
		       __UPDATE_ENABLED__,				\
		       __MATCH_AUX__, __WHERECLAUSE__)			\
  static dns_qtype_map __RR_TYPE_##__TYPENAME__ = { "" #__TYPENAME__ "", \
						    DNS_QTYPE_##__TYPENAME__, \
						    __PERSISTENT__, \
						    __PARSER__, \
						    __FREE__, \
						    __DUPLICATOR__, \
						    __SIZOR__, \
						    NULL, \
						    NULL, \
						    NULL, \
						    NULL, \
						    __EXPORT_BIND_RR__, \
						    __EXPORT_TINYDNS_RR__, \
						    __UPDATE_ENABLED__, \
						    __MATCH_AUX__, \
						    __WHERECLAUSE__ }

#include "rrtype.h"

#if DEBUG_ENABLED
int		debug_mydnsexport = 0;

debugging_switch debugging_switches[] = {
  {	"all",			&debug_all },

  {	"export",		&debug_export },
  {	"util",			&debug_util },

  {	"mydnsexport",		&debug_mydnsexport },

  {	NULL,			NULL }

};

#endif

char *zone = NULL;						/* Zone name to dump */
unsigned int zones_out = 0;					/* Number of zones output */
enum _output_format {						/* Output format types */
	OUTPUT_BIND,
	OUTPUT_TINYDNS
} output_format = OUTPUT_BIND;

char  *thishostname;	                                        /* Hostname of local machine */
char *command_line = NULL;					/* Full command line */

#define DNSBUFLEN	DNS_MAXNAMELEN+1


/**********************************************************************************************
	USAGE
	Display program usage information.
**********************************************************************************************/
static void
usage(int status, char *unrecognized_option) {
  if (status != EXIT_SUCCESS) {
    if (unrecognized_option) {
      fprintf(stderr, _("unrecognized option '%s'\n"), unrecognized_option);
    }
    fprintf(stderr, _("Try `%s --help' for more information."), progname);
    fputs("\n", stderr);
  } else {
    printf(_("Usage: %s [ZONE]..."), progname);
    puts("");
    puts(_("Output MyDNS zone in formats understood by other DNS servers."));
    puts("");
    puts(_("  -b, --bind              output in BIND format (the default)"));
    puts(_("  -t, --tinydns-data      output in tinydns-data format"));
    puts("");
    puts(_("  -c, --conf=FILE         read config from FILE instead of the default"));
    puts(_("  -D, --database=DB       database name to use"));
    puts(_("  -h, --host=HOST         connect to SQL server at HOST"));
    puts(_("  -p, --password=PASS     password for SQL server (or prompt from tty)"));
    puts(_("  -u, --user=USER         username for SQL server if not current user"));
    puts("");
#if DEBUG_ENABLED
    puts(_("  -d, --debug             enable debug output"));
#endif
    puts(_("  -v, --verbose           be more verbose while running"));
    puts(_("      --help              display this help and exit"));
    puts(_("      --version           output version information and exit"));
    puts("");
    printf(_("Report bugs to <%s>.\n"), PACKAGE_BUGREPORT);
  }
  exit(status);
}
/*--- usage() -------------------------------------------------------------------------------*/


/**********************************************************************************************
	CMDLINE
	Process command line options.
**********************************************************************************************/
static void
cmdline(int argc, char **argv) {
  char	*optstr;
  int	optc, optindex, n;
  struct option const longopts[] = {
    {"bind",		no_argument,		NULL,	'b'},
    {"conf",		required_argument,	NULL,	'c'},
    {"database",	required_argument,	NULL,	'D'},
    {"host",		required_argument,	NULL,	'h'},
    {"password",	optional_argument,	NULL,	'p'},
    {"tinydns-data",	no_argument,		NULL,	't'},
    {"user",		required_argument,	NULL,	'u'},

    {"debug",		no_argument,		NULL,	'd'},
    {"verbose",		no_argument,		NULL,	'v'},
    {"help",		no_argument,		NULL,	0},
    {"version",		no_argument,		NULL,	0},

    {NULL,		0,			NULL,	0}
  };

  /* Copy full command line into 'command_line' */
  for (n = 0; n < argc; n++) {
    if (!command_line) 	{
      command_line = STRDUP(argv[n]);
    } else {
      command_line = REALLOCATE(command_line, strlen(command_line) + strlen(argv[n]) + 2, char*);
      strcat(command_line, " ");
      strcat(command_line, argv[n]);
    }
  }

  thishostname = ALLOCATE(DNS_MAXNAMELEN +1, char*);
  gethostname(thishostname, DNS_MAXNAMELEN);

  err_file = stdout;

  error_init(argv[0], LOG_USER);				/* Init output routines */

  optstr = getoptstr(longopts);
  opterr = 0;

  while ((optc = getopt_long(argc, argv, optstr, longopts, &optindex)) != -1) {
    switch (optc) {
    case 0:
      {
	const char *opt = longopts[optindex].name;

	if (!strcmp(opt, "version")) {				/* --version */
	  printf("%s ("PACKAGE_NAME") "PACKAGE_VERSION" ("SQL_VERSION_STR")\n", progname);
	  puts("\n" PACKAGE_COPYRIGHT);
	  puts(_("This is free software; see the source for copying conditions.  There is NO"));
	  puts(_("warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE."));
	  exit(EXIT_SUCCESS);
	} else if (!strcmp(opt, "help"))			/* --help */
	  usage(EXIT_SUCCESS, NULL);
      }
      break;

    case 'c':						/* -c, --conf=FILE */
      opt_conf = optarg;
      break;

    case 'd':							/* -d, --debug */
#if DEBUG_ENABLED
      err_verbose = err_debug = 1;
#endif
      break;
    case 'D':							/* -D, --database=DB */
      conf_set(&Conf, "database", optarg, 0);
      break;
    case 'h':							/* -h, --host=HOST */
      conf_set(&Conf, "db-host", optarg, 0);
      break;
    case 'p':							/* -p, --password=PASS */
      if (optarg) {
	conf_set(&Conf, "db-password", optarg, 0);
	memset(optarg, 'X', strlen(optarg));
      }	else
	conf_set(&Conf, "db-password", passinput(_("Enter password")), 0);
      break;
    case 'u':							/* -u, --user=USER */
      conf_set(&Conf, "db-user", optarg, 0);
      break;

    case 'b':							/* -b, --bind */
      output_format = OUTPUT_BIND;
      break;
    case 't':							/* -t, --tinydns-data */
      output_format = OUTPUT_TINYDNS;
      break;

    case 'v':							/* -v, --verbose */
      err_verbose = 1;
      break;
    case '?':
#if DEBUG_ENABLED
      if (debug_parse(argc, argv)) {
	break;
      }
#endif

    default:
      usage(EXIT_FAILURE, argv[optind-1]);
    }
  }
  load_config();
}
/*--- cmdline() ---------------------------------------------------------------------------------*/


/**************************************************************************************************
	DUMP_HEADER
	Dumps a header.  Currently only writes a header for tinydns-data format, since BIND format
	puts a header at the start of each "file".
**************************************************************************************************/
static void
dump_header(void) {
  time_t now = time(NULL);

  switch (output_format) {
  case OUTPUT_BIND:
    break;
  case OUTPUT_TINYDNS:
    printf("#\n");
    printf("# Created by \"%s\"\n", command_line);
    printf("# %.24s\n", ctime(&now));
    printf("#\n");
    break;
  }
}
/*--- dump_header() -----------------------------------------------------------------------------*/


/**************************************************************************************************
	BIND_DUMP_SOA
	Output SOA, BIND format.
**************************************************************************************************/
static void
bind_dump_soa(MYDNS_SOA *soa) {
  time_t now = time(NULL);

  if (zones_out > 0)
    puts("\f");

  printf("$TTL %u\n", soa->ttl);
  printf("; Zone: %s (#%u)\n", soa->origin, soa->id);
  printf("; Created by \"%s\"\n", command_line);
  printf("; %.24s\n", ctime(&now));
  printf("$ORIGIN %s\n\n", soa->origin);

  printf("@\tIN SOA\t%s\t%s (\n", soa->ns, soa->mbox);
  printf("\t%-10u\t  ; Serial\n", soa->serial);
  printf("\t%-10u\t  ; Refresh\n", soa->refresh);
  printf("\t%-10u\t  ; Retry\n", soa->retry);
  printf("\t%-10u\t  ; Expire\n", soa->expire);
  printf("\t%-10u\t) ; Minimum\n\n", soa->minimum);
}
/*--- bind_dump_soa() ---------------------------------------------------------------------------*/

/**************************************************************************************************
	BIND_DUMP_RR
	Output resource record, BIND format.
**************************************************************************************************/
static void
bind_dump_rr(MYDNS_SOA *soa, MYDNS_RR *rr, int maxlen) {
  dns_qtype_map *map;
  int ttl, aux;

  map = mydns_rr_get_type_by_id((rr->alias == 0) ? rr->type : DNS_QTYPE_CNAME);

  ttl = (rr->ttl < soa->minimum) ? soa->minimum : rr->ttl;
  aux = rr->aux;

  map->rr_export_bind_rr(soa, rr,
			 MYDNS_RR_NAME(rr), MYDNS_RR_DATA_VALUE(rr), MYDNS_RR_DATA_LENGTH(rr),
			 ttl, aux, maxlen);
}
/*--- bind_dump_rr() ----------------------------------------------------------------------------*/

/**************************************************************************************************
	TINYDNS_DUMP_SOA
**************************************************************************************************/
static void
tinydns_dump_soa(MYDNS_SOA *soa) {
  char *origin, *ns, *mbox;

  origin = STRDUP(soa->origin);
  ns = STRDUP(soa->ns);
  mbox = STRDUP(soa->mbox);

  if (*origin && LASTCHAR(origin) == '.') LASTCHAR(origin) = '\0';
  if (*ns && LASTCHAR(ns) == '.') LASTCHAR(ns) = '\0';
  if (*mbox && LASTCHAR(mbox) == '.') LASTCHAR(mbox) = '\0';

  printf("Z%s:%s:%s:%u:%u:%u:%u:%u:%u\n",
	 origin, ns, mbox,
	 soa->serial, soa->refresh, soa->retry, soa->expire, soa->minimum, soa->ttl); 
  RELEASE(origin);
  RELEASE(ns);
  RELEASE(mbox);
}
/*--- tinydns_dump_soa() ------------------------------------------------------------------------*/

/**************************************************************************************************
	TINYDNS_DUMP_RR
	Output resource record, BIND format.
**************************************************************************************************/
static void
tinydns_dump_rr(MYDNS_SOA *soa, MYDNS_RR *rr) {
  dns_qtype_map *map;
  char *name = NULL, *data = NULL;

  map = mydns_rr_get_type_by_id(rr->type);

  name = STRDUP(MYDNS_RR_NAME(rr));
  TINYDNS_NAMEFIX(name);

  data = ALLOCATE(MYDNS_RR_DATA_LENGTH(rr)+1, char*);
  memset(data, '\0', MYDNS_RR_DATA_LENGTH(rr));
  memcpy(data, MYDNS_RR_DATA_VALUE(rr), MYDNS_RR_DATA_LENGTH(rr));

  map->rr_export_tinydns_rr(soa, rr, name, data, MYDNS_RR_DATA_LENGTH(rr), rr->ttl, rr->aux);

}
/*--- tinydns_dump_rr() -------------------------------------------------------------------------*/


/**************************************************************************************************
	DUMP_SOA
**************************************************************************************************/
static MYDNS_SOA *
dump_soa(void) {
  MYDNS_SOA *soa;

  if (mydns_soa_load(sql, &soa, zone) != 0)
    ErrSQL(sql, "%s: %s", zone, _("error loading SOA record for zone"));
  if (!soa)
    Errx("%s: %s", zone, _("zone not found"));

  switch (output_format) {
  case OUTPUT_BIND:
    bind_dump_soa(soa);
    break;
  case OUTPUT_TINYDNS:
    tinydns_dump_soa(soa);
    break;
  }

  return (soa);
}
/*--- dump_soa() --------------------------------------------------------------------------------*/


/**************************************************************************************************
	DUMP_RR
**************************************************************************************************/
static void
dump_rr(MYDNS_SOA *soa, MYDNS_RR *rr, int maxlen) {
  switch (output_format) {
  case OUTPUT_BIND:
    bind_dump_rr(soa, rr, maxlen);
    break;
  case OUTPUT_TINYDNS:
    tinydns_dump_rr(soa, rr);
    break;
  }
}
/*--- dump_rr() ---------------------------------------------------------------------------------*/


/**************************************************************************************************
	DUMP_RR_LONG
**************************************************************************************************/
static void
dump_rr_long(MYDNS_SOA *soa) {
  int maxlen = 0;
  char *query;
  SQL_RES *res;
  SQL_ROW row;
  unsigned long *lengths;
  char		*columns = NULL;

  /* No records in zone - return immediately */
  if (!sql_count(sql, "SELECT COUNT(*) FROM %s WHERE zone=%u", mydns_rr_table_name, soa->id)) {
    if (output_format == OUTPUT_BIND)
      puts("");
    return;
  }

  if (output_format == OUTPUT_BIND)
    maxlen = sql_count(sql, "SELECT MAX(LENGTH(name)) FROM %s WHERE zone=%u", mydns_rr_table_name, soa->id) + 1;

  if (!maxlen)
    maxlen = DNS_MAXNAMELEN;

  columns = mydns_rr_columns();

  query = mydns_rr_prepare_query(soa->id, DNS_QTYPE_ANY, NULL, soa->origin, mydns_rr_active_types[0],
				   columns, NULL);
  RELEASE(columns);

#ifdef notdef
  querylen = ASPRINTF(&query,
		      "SELECT "MYDNS_RR_FIELDS" FROM %s WHERE zone=%u ORDER BY name,type,aux",
		      mydns_rr_table_name, soa->id);
#endif

  /* Submit query */
  res = sql_query(sql, query, strlen(query));
  RELEASE(query);
  if (!res)
    return;

  /* Add results to list */
  while ((row = sql_getrow(res, &lengths))) {
    MYDNS_RR *rr;

    if (!(rr = mydns_rr_parse(row, lengths, soa->origin)))
      continue;
    dump_rr(soa, rr, maxlen);
    mydns_rr_free(rr);		
  }
  sql_free(res);

  if (output_format == OUTPUT_BIND)
    puts("");
}
/*--- dump_rr_long() ----------------------------------------------------------------------------*/


/**************************************************************************************************
	DUMP_ZONE
**************************************************************************************************/
static void
dump_zone(char *zone_name) {
  MYDNS_SOA *soa;
  int zonelen = (strlen(zone_name) + ((LASTCHAR(zone_name) != '.')?2:1));
  zone = ALLOCATE(zonelen, char*);
  memset(zone, 0, zonelen);
  strcpy(zone, zone_name);
  if (LASTCHAR(zone) != '.')
    strcat(zone, ".");
  if ((soa = dump_soa())) {
    dump_rr_long(soa);
    mydns_soa_free(soa);
  }
  RELEASE(zone);
  zones_out++;
}
/*--- dump_zone() -------------------------------------------------------------------------------*/


/**************************************************************************************************
	MAIN
**************************************************************************************************/
int
main(int argc, char **argv) {
#if DEBUG_ENABLED
  debug_start(debugging_switches);
#endif
  setlocale(LC_ALL, "");					/* Internationalization */
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);
  cmdline(argc, argv);

  db_connect();

  db_check_optional();

  dump_header();
  if (optind >= argc) {
    SQL_RES *res;
    SQL_ROW row;
    char *query;
    size_t querylen;

    querylen = ASPRINTF(&query, "SELECT origin FROM %s", mydns_soa_table_name);
    res = sql_query(sql, query, querylen);
    RELEASE(query);
    if (!res)
      return (0);

    while ((row = sql_getrow(res, NULL)))
      dump_zone(row[0]);
    sql_free(res);
  }

  while (optind < argc)
    dump_zone((char *)argv[optind++]);
 
  return (0);
}
/*--- main() ------------------------------------------------------------------------------------*/

/* vi:set ts=3: */
/* NEED_PO */