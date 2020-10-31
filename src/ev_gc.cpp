#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <raikv/ev_net.h>
#include <raist/ev_gc.h>
#include <raimd/md_list.h>
#include <raimd/md_hash.h>
#include <raimd/md_set.h>
#include <raimd/md_zset.h>

using namespace rai;
using namespace st;
using namespace kv;
using namespace md;

static const uint64_t GC_TIMER_USECS = 300;

EvGc::EvGc( EvPoll &p,  uint64_t scan_time ) noexcept
    : EvSocket( p, p.register_type( "gc" ) ),
      kctx( *p.map, p.dbx_id, NULL ),
      scan_pos( 0 ), gc_time( 0 ), gc_cnt( 0 ), start_time( 0 ), ten_ns( 0 ),
      verbose( 1 )
{
  this->timer_id   = (uint64_t) this->sock_type << 56;
  this->start_time = current_monotonic_time_ns();
  this->scan_cnt   = p.map->hdr.ht_size /
                     ( scan_time / ( GC_TIMER_USECS * (uint64_t) 1000 ) ) + 1;
  this->kctx.set( KEYCTX_IS_GC_ACQUIRE | KEYCTX_NO_COPY_ON_READ |
                  KEYCTX_IS_CUCKOO_ACQUIRE );
  for ( int i = 0; i < 256; i++ )
    this->db_to_dbx_id[ i ] = KV_NO_DBSTAT_ID;
  this->db_to_dbx_id[ this->kctx.db_num ] = p.dbx_id;
}

EvGc *
EvGc::create_gc( EvPoll &p,  uint64_t scan_time ) noexcept
{
  void * m = aligned_malloc( sizeof( EvGc ) );
  if ( m == NULL ) {
    perror( "alloc gc" );
    return NULL;
  }
  EvGc * g = new ( m ) EvGc( p, scan_time );
  int pfd = p.get_null_fd();
  g->PeerData::init_peer( pfd, NULL, "gc" );
  g->sock_opts = kv::OPT_NO_POLL | kv::OPT_NO_CLOSE;
  if ( p.add_sock( g ) < 0 ) {
    printf( "failed to add gc\n" );
    return NULL;
  }
  p.add_timer_micros( pfd, GC_TIMER_USECS, g->timer_id, 0 );
  return g;
}

void EvGc::write( void ) noexcept {}
void EvGc::read( void ) noexcept {}
void EvGc::process( void ) noexcept {}
void EvGc::release( void ) noexcept {}

inline uint32_t
EvGc::get_dbx_id( void ) noexcept
{
  uint8_t db_num = this->kctx.get_db();
  if ( this->db_to_dbx_id[ db_num ] != KV_NO_DBSTAT_ID )
    return this->db_to_dbx_id[ db_num ];
  return this->db_to_dbx_id[ db_num ] =
    this->kctx.ht.attach_db( this->kctx.ctx_id, db_num );
}

void
EvGc::expire( uint32_t dbx_id ) noexcept
{
  if ( dbx_id == KV_NO_DBSTAT_ID )
    return;
  KeyCtx kctx2( this->kctx.ht, dbx_id, NULL );
  kctx2.init_work( &this->wrk );
  if ( kctx2.try_acquire_position( this->kctx.pos ) != KEY_BUSY ) {
    if ( kctx2.is_expired() )
      kctx2.expire();
    kctx2.release();
  }
}

void
EvGc::set_lru_clock( uint32_t dbx_id ) noexcept
{
  if ( dbx_id == KV_NO_DBSTAT_ID )
    return;
  WorkAllocT< 256 > tmp_wrk;
  KeyCtx kctx2( this->kctx.ht, dbx_id, NULL );
  kctx2.init_work( &tmp_wrk );
  if ( kctx2.try_acquire_position( this->kctx.pos ) != KEY_BUSY ) {
    if ( this->kctx.key == kctx2.key && this->kctx.key2 == kctx2.key2 &&
         this->kctx.serial == kctx2.serial ) {
      kctx2.entry->set( FL_CLOCK );
    }
    kctx2.release();
  }
}

template <class LIST_CLASS>
struct GcListCtx {
  LIST_CLASS    x;
  KeyCtx      & kctx;
  KeyFragment * kbuf;
  size_t        count,
                data_len,
                used;

  GcListCtx( KeyCtx &k ) : kctx( k ), kbuf( 0 ), count( 0 ), data_len( 0 ),
    used( 0 ) {}

  bool open_readonly( void ) {
    uint8_t  lhdr[ LIST_HDR_OOB_SIZE ];
    void   * data    = NULL;
    size_t   datalen = 0;
    uint64_t llen    = sizeof( lhdr );

    if ( this->kctx.value_copy( &data, datalen, lhdr, llen ) == KEY_OK ) {
      new ( &this->x ) LIST_CLASS( data, datalen );
      this->x.open( lhdr, llen );
      return true;
    }
    return false;
  }

  bool can_resize( void ) {
    this->used = this->x.used_size( this->count, this->data_len );
    if ( this->used >= this->x.size )
      return false;
    if ( this->kctx.get_key( this->kbuf ) != KEY_OK )
      return false;
    /* key fetch validates value when not immediate */
    if ( this->kctx.entry->test( FL_IMMEDIATE_KEY ) == 0 &&
         this->kctx.validate_value() != KEY_OK ) /* if not mutated */
      return false;
    return true;
  }

  bool resize( uint32_t dbx_id )
  {
    if ( dbx_id == KV_NO_DBSTAT_ID )
      return false;
    WorkAllocT< 256 > tmp_wrk;
    LIST_CLASS y;
    MsgCtx     mctx( this->kctx.ht, dbx_id );
    KeyCtx     kctx2( this->kctx.ht, dbx_id, NULL );
    void     * new_data;
    bool       success = false;

    mctx.set_key( *this->kbuf );
    mctx.set_hash( this->kctx.key, this->kctx.key2 );
    if ( mctx.alloc_segment( &new_data, this->used, 0 ) != KEY_OK )
      return false;
    new ( &y ) LIST_CLASS( new_data, this->used );
    y.init( this->count, this->data_len );

    kctx2.init_work( &tmp_wrk );
    kctx2.set_key( *this->kbuf );
    if ( kctx2.try_acquire_position( this->kctx.pos ) != KEY_BUSY ) {
      if ( this->kctx.key == kctx2.key && this->kctx.key2 == kctx2.key2 &&
           this->kctx.serial == kctx2.serial ) {
        this->x.copy( y );
        kctx2.load( mctx );
        kctx2.entry->set( FL_CLOCK );
        success = true;
      }
      kctx2.release();
    }
    if ( ! success ) {
      mctx.nevermind();
      return false;
    }
    return true;
  }
};

bool
EvGc::resize( uint32_t dbx_id ) noexcept
{
  switch ( this->kctx.get_type() ) {
    case MD_LIST: {
      GcListCtx<ListData> list( this->kctx );
      if ( ! list.open_readonly() || ! list.can_resize() )
        break;
      if ( list.resize( dbx_id ) ) {
        if ( this->verbose > 0 ) {
          printf( "list %.*s resized %lu < %lu\n",
                  (int) list.kbuf->keylen, list.kbuf->u.buf,
                  list.used, list.x.size );
        }
      }
      return true;
    }
    case MD_HASH: {
      GcListCtx<HashData> hash( this->kctx );
      if ( ! hash.open_readonly() || ! hash.can_resize() )
        break;
      if ( hash.resize( dbx_id ) ) {
        if ( this->verbose > 0 ) {
          printf( "hash %.*s resized %lu < %lu\n",
                  (int) hash.kbuf->keylen, hash.kbuf->u.buf,
                  hash.used, hash.x.size );
        }
      }
      return true;
    }
    case MD_SET: {
      GcListCtx<SetData> set( this->kctx );
      if ( ! set.open_readonly() || ! set.can_resize() )
        break;
      if ( set.resize( dbx_id ) ) {
        if ( this->verbose > 0 ) {
          printf( "set %.*s resized %lu < %lu\n",
                  (int) set.kbuf->keylen, set.kbuf->u.buf,
                  set.used, set.x.size );
        }
      }
      return true;
    }
    case MD_ZSET: {
      GcListCtx<ZSetData> zset( this->kctx );
      if ( ! zset.open_readonly() || ! zset.can_resize() )
        break;
      if ( zset.resize( dbx_id ) ) {
        if ( this->verbose > 0 ) {
          printf( "zset %.*s resized %lu < %lu\n",
                  (int) zset.kbuf->keylen, zset.kbuf->u.buf,
                  zset.used, zset.x.size );
        }
      }
      return true;
    }
    default:
      break;
  }
  return false;
}

inline void
EvGc::check( uint64_t pos ) noexcept
{
  KeyStatus status = this->kctx.fetch( &this->wrk, pos, false );
  if ( status != KEY_OK )
    return;
  if ( this->kctx.entry->test( FL_DROPPED ) )
    return;
  /* check if expired */
  if ( this->kctx.entry->test( FL_EXPIRE_STAMP ) &&
       this->kctx.check_expired() == KEY_EXPIRED ) {
    this->expire( this->get_dbx_id() );
    return;
  }
  /* if already checked */
  if ( this->kctx.entry->test( FL_CLOCK ) )
    return;
  if ( this->kctx.entry->test( FL_SEGMENT_VALUE ) ) {
    /* if last update was at least 10 seconds ago */
    if ( this->kctx.check_update( this->ten_ns ) == KEY_EXPIRED )
      if ( this->resize( this->get_dbx_id() ) )
        return;
  }
  /* mark it with FL_CLOCK */
  this->set_lru_clock( this->get_dbx_id() );
}

inline const uint8_t *
EvGc::prefetch( const uint8_t *p,  size_t sz ) const noexcept
{
  static const int locality = 3; /* 0 is non, 1 is low, 2 is moderate, 3 high*/
  __builtin_prefetch( p, 0, locality );
  return &p[ sz ];
}

bool
EvGc::timer_expire( uint64_t,  uint64_t ) noexcept
{
  static const size_t PREFETCH_CNT = 16;
  uint64_t  start    = current_monotonic_time_ns(),
            pos      = this->scan_pos,
            ht_size  = this->kctx.ht.hdr.ht_size,
            now,
            end_pos;
  const size_t    sz = this->kctx.hash_entry_size;
  const uint8_t * p  = (const uint8_t *) this->kctx.ht.get_entry( pos, sz );
  bool      edge     = false;

  this->ten_ns = this->kctx.ht.hdr.current_stamp - (uint64_t) 10 * NANOS;
  //scan_cnt *= ( this->kctx.ht.hdr.load_percent / 64 + 1 ); /* 16 secs if full */
  end_pos   = this->scan_cnt + pos + PREFETCH_CNT;
  if ( end_pos > ht_size ) {
    end_pos = ht_size;
    edge    = ( pos + PREFETCH_CNT > ht_size );
  }
  else {
    end_pos -= PREFETCH_CNT;
  }
  if ( ! edge ) {
    for ( size_t i = 0; i < PREFETCH_CNT - 1; i++ )
      p = this->prefetch( p, sz );
    while ( pos + PREFETCH_CNT < end_pos ) {
      p = this->prefetch( p, sz );
      this->check( pos++ );
    }
  }
  while ( pos < end_pos )
    this->check( pos++ );
  now = current_monotonic_time_ns();
  this->gc_time += now - start;
  this->gc_cnt  += 1;
  if ( pos == ht_size ) {
    if ( this->verbose > 1 ) {
       printf( "timer %lu avg %lu real %.2f busy %.3f%%\n", this->gc_time,
            this->gc_time / this->gc_cnt,
            (double) ( now - this->start_time ) / 1000000000.0,
            (double) this->gc_time * 100.0 /
            (double) ( now - this->start_time ) );
    }
    this->gc_time    = 0;
    this->gc_cnt     = 0;
    this->start_time = now;
    this->scan_pos   = 0;
  }
  else {
    this->scan_pos = pos;
  }
  return true;
}

