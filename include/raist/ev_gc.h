#ifndef __rai_raist__ev_gc_h__
#define __rai_raist__ev_gc_h__

namespace rai {
namespace st {

struct EvGc : public kv::EvSocket {
  kv::KeyCtx kctx;
  uint64_t   timer_id,
             scan_pos,
             scan_cnt,
             gc_time,
             gc_cnt,
             start_time,
             ten_ns;
  int        verbose;
  uint32_t   db_to_dbx_id[ 256 ];
  kv::WorkAllocT< 1024 > wrk;

  void * operator new( size_t, void *ptr ) { return ptr; }
  EvGc( kv::EvPoll &p,  uint64_t scan_time ) noexcept;
  static EvGc *create_gc( kv::EvPoll &p,  uint64_t scan_time ) noexcept;
  void expire( uint32_t dbx_id ) noexcept;
  void set_lru_clock( uint32_t dbx_id ) noexcept;
  bool resize( uint32_t dbx_id ) noexcept;
  uint32_t get_dbx_id( void ) noexcept;
  void check( uint64_t pos ) noexcept;
  const uint8_t * prefetch( const uint8_t *p,  size_t sz ) const noexcept;
  /* EvSocket */
  virtual void write( void ) noexcept final;
  virtual void read( void ) noexcept final;
  virtual void process( void ) noexcept final;
  virtual void release( void ) noexcept final;
  virtual bool timer_expire( uint64_t timer_id, uint64_t event_id ) noexcept;
};

}
}
#endif
