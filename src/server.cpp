#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>
#include <raikv/ev_net.h>
#include <raikv/monitor.h>
#include <raist/ev_gc.h>

using namespace rai;
using namespace st;
using namespace kv;

static const char *
get_arg( int argc, const char *argv[], int b, const char *f, const char *def )
{
  for ( int i = 1; i < argc - b; i++ )
    if ( ::strcmp( f, argv[ i ] ) == 0 ) /* -m map -p port */
      return argv[ i + b ];
  return def; /* default value */
}

int
main( int argc, const char *argv[] )
{
  HashTabGeom   geom;
  HashTab     * map        = NULL;
  double        ratio      = 0.5;
  uint64_t      stats_ival = NANOS,
                check_ival = NANOS / 10,
                scan_time  = (uint64_t) 60 * NANOS;
  uint64_t      mbsize     = 1024 * 1024 * 1024; /* 1G */
  uint32_t      entsize    = 64,                 /* 64b */
                valsize    = 1024 * 1024;        /* 1MB */
  uint8_t       arity      = 2;                  /* cuckoo 2+4 */
  uint16_t      buckets    = 4;

  const char * mn = get_arg( argc, argv, 1, "-m", KV_DEFAULT_SHM ),
             * mb = get_arg( argc, argv, 1, "-s", "2048" ),
             * pc = get_arg( argc, argv, 1, "-k", "0.25" ),
             * cu = get_arg( argc, argv, 1, "-c", "2+4" ),
             * mo = get_arg( argc, argv, 1, "-o", "ug+rw" ),
             * vz = get_arg( argc, argv, 1, "-v", "2048" ),
             * ez = get_arg( argc, argv, 1, "-e", "64" ),
             * at = get_arg( argc, argv, 0, "-a", 0 ),
             * rm = get_arg( argc, argv, 0, "-r", 0 ),
             * iv = get_arg( argc, argv, 1, "-i", "1" ),
             * ix = get_arg( argc, argv, 1, "-x", "0.1" ),
             * gs = get_arg( argc, argv, 1, "-g", "60" ),
             * he = get_arg( argc, argv, 0, "-h", 0 );

  if ( he != NULL ) {
  cmd_error:;
    fprintf( stderr,
  "%s [-m map] [-s MB] [-k ratio] [-c cuckoo a+b] "
     "[-v value-sz] [-e entry-sz] [-a] [-r] [-i secs] [-x secs] [-g secs]\n"
  "  -m map         = name of map file (default: " KV_DEFAULT_SHM ")\n"
  "  -s MB          = size of HT (MB * 1024 * 1024, default: 1024)\n"
  "  -k ratio       = entry to segment memory ratio (float 0 -> 1, def: 0.5)\n"
  "                  (1 = all ht, 0 = all msg -- must have some ht)\n"
  "  -c cuckoo a+b  = cuckoo hash arity and buckets (default: 2+4)\n"
  "  -o mode        = create map using mode (default: ug+rw)\n"
  "  -v value-sz    = max value size or min seg size (in KB, default: 1024)\n"
  "  -e entry-sz    = hash entry size (multiple of 64, default: 64)\n"
  "  -a             = attach to map, don't create (default: create)\n"
  "  -r             = remove map and then exit\n"
  "  -i secs        = stats interval (default: 1)\n"
  "  -x secs        = check interval (default: 0.1)\n"
  "  -g secs        = gc table scan complete time (default: 60)\n",
             argv[ 0 ] );
    return 1;
  }

  mbsize = (uint64_t) ( strtod( mb, 0 ) * (double) ( 1024 * 1024 ) );
  if ( mbsize == 0 )
    goto cmd_error;
  ratio = strtod( pc, 0 );
  if ( ratio < 0.0 || ratio > 1.0 )
    goto cmd_error;
  /* look for arity+buckets */
  if ( isdigit( cu[ 0 ] ) && cu[ 1 ] == '+' && isdigit( cu[ 2 ] ) ) {
    arity   = cu[ 0 ] - '0'; 
    buckets = atoi( &cu[ 2 ] );
  }
  else { 
    goto cmd_error;
  }
  valsize = (uint32_t) atoi( vz ) * (uint32_t) 1024;
  if ( valsize == 0 && ratio < 1.0 )
    goto cmd_error;
  entsize = (uint32_t) atoi( ez );
  if ( entsize == 0 )
    goto cmd_error;

  stats_ival = (uint64_t) ( strtod( iv, 0 ) * NANOSF );
  check_ival = (uint64_t) ( strtod( ix, 0 ) * NANOSF );
  scan_time  = (uint64_t) ( strtod( gs, 0 ) * NANOSF );
  if ( stats_ival == 0 || check_ival == 0 || scan_time == 0 ) {
    fprintf( stderr, "interval must not be zero (-i -x or -g)\n" );
    goto cmd_error;
  }

  if ( at == NULL && rm == NULL ) {
    int mode, x;
    geom.map_size         = mbsize; 
    geom.max_value_size   = ratio < 0.999 ? valsize : 0; 
    geom.hash_entry_size  = align<uint32_t>( entsize, 64 );
    geom.hash_value_ratio = ratio;
    geom.cuckoo_buckets   = buckets;
    geom.cuckoo_arity     = arity;
    mode = atoi( mo );
    if ( mode == 0 ) {
      x = mode = 0;
      if ( ::strchr( mo, 'r' ) != NULL ) x = 4;
      if ( ::strchr( mo, 'w' ) != NULL ) x |= 2;
      if ( ::strchr( mo, 'u' ) != NULL ) mode |= x << 6;
      if ( ::strchr( mo, 'g' ) != NULL ) mode |= x << 3;
      if ( ::strchr( mo, 'o' ) != NULL ) mode |= x;
    }
    if ( mode == 0 ) {
      fprintf( stderr, "Invalide create map mode: %s (0%o)\n", mo, mode );
      goto cmd_error;
    }
    printf( "Creating map %s, mode %s (0%o)\n", mn, mo, mode );
    map = HashTab::create_map( mn, 0, geom, mode );
  }
  else if ( at != NULL ) {
    printf( "Attaching map %s\n", mn );
    map = HashTab::attach_map( mn, 0, geom );
  }
  else {
    if ( HashTab::remove_map( mn, 0 ) == 0 ) {
      printf( "removed %s\n", mn );
      return 0;
    }
    printf( "failed to remove %s\n", mn );
    /* return 1 below */
  }
  if ( map == NULL )
    return 1;
  //print_map_geom( map, MAX_CTX_ID );

  Monitor       svr( *map, stats_ival, check_ival );
  EvPoll        poll;
  EvShm         shm( "st_server", map );
  SignalHandler sighndl;
  EvGc        * gc = NULL;
  char          junk[ 8 ];
  ssize_t       nbytes;
  bool          tty    = false;
  int           status = 0;

  if ( shm.attach( 0 ) != 0 ||       /* attach to db */
       poll.init( 5, false ) != 0 || /* init epoll */
       poll.sub_route.init_shm( shm ) != 0 )   /* init kv pubsub */
    status = 1;
  /* create gc and start timer */
  if ( status == 0 && (gc = EvGc::create_gc( poll, scan_time )) == NULL )
    status = 1;

  if ( status == 0 ) {
    if ( isatty( 0 ) ) {
      fcntl( 0, F_SETFL, fcntl( 0, F_GETFL, 0 ) | O_NONBLOCK );
      tty = true;
    }
    sighndl.install();
    for (;;) {
      if ( tty && (nbytes = read( 0, junk, sizeof( junk ) )) > 0 )
        svr.stats_counter = 0; /* print header again */
      svr.interval_update(); /* this updates current_stamp, better more often */
      if ( poll.quit >= 5 )
        break;
      int state = poll.dispatch(); /* 0 if idle, 1, 2, 3 if busy */
      /* gc timer will expire if idle */
      poll.wait( state == EvPoll::DISPATCH_IDLE ? 100 : 0 );
      if ( sighndl.signaled && ! poll.quit )
        poll.quit++;
    }
  }
  shm.close();
  return status;
}

