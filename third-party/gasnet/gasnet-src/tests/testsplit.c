/* $Source: bitbucket.org:berkeleylab/gasnet.git/tests/testsplit.c $
 * Copyright (c) 2018, The Regents of the University of California
 */

#include <gasnetex.h>

#define SCRATCH_SIZE (2*1024*1024)

#ifndef TEST_SEGSZ
#define TEST_SEGSZ (PAGESZ + 6*SCRATCH_SIZE) // 6 teams's scratch + page for comms
#endif

#include <math.h> /* for sqrt() */
#include <test.h>

#ifndef SCRATCH_QUERY_FLAG
#define SCRATCH_QUERY_FLAG GEX_FLAG_TM_SCRATCH_SIZE_RECOMMENDED
#endif

static gex_Client_t  myclient;
static gex_EP_t      myep;
static gex_TM_t      myteam, rowtm, coltm;
static gex_Segment_t mysegment;
static gex_Rank_t    myrank;

static uintptr_t scratch_addr, scratch_end;

// handler indices
#define hidx_pong_shorthandler       200
#define hidx_rank_shorthandler       201
#define hidx_rank_medlonghandler     202

// handler functions
static gasnett_atomic_t am_cntr = gasnett_atomic_init(0);
void pong_shorthandler(gex_Token_t token, gex_AM_Arg_t arg0) {
  gex_Token_Info_t info;
  gex_TI_t rc = gex_Token_Info(token, &info, GEX_TI_SRCRANK);
  assert_always((gex_Rank_t)arg0 == info.gex_srcrank);
  gasnett_atomic_increment(&am_cntr, 0);
}
void am_validate(gex_Token_t token, gex_AM_Arg_t arg0, gex_AM_Arg_t arg1) {
  assert_always((gex_Rank_t)arg0 == myrank);
  gex_Token_Info_t info;
  gex_TI_t rc = gex_Token_Info(token, &info, GEX_TI_SRCRANK);
  assert_always((gex_Rank_t)arg1 == info.gex_srcrank);
  gex_AM_ReplyShort1(token, hidx_pong_shorthandler, 0, (gex_AM_Arg_t)myrank);
}
void rank_shorthandler(gex_Token_t token, gex_AM_Arg_t arg0, gex_AM_Arg_t arg1) {
  am_validate(token, arg0, arg1);
}
void rank_medlonghandler(gex_Token_t token, void *buf, size_t nbytes,
                         gex_AM_Arg_t arg0, gex_AM_Arg_t arg1) {
  am_validate(token, arg0, arg1);
}

// handler table
gex_AM_Entry_t htable[] = {
  { hidx_pong_shorthandler,   pong_shorthandler,   GEX_FLAG_AM_REPLY  |GEX_FLAG_AM_SHORT,   1 },
  { hidx_rank_shorthandler,   rank_shorthandler,   GEX_FLAG_AM_REQUEST|GEX_FLAG_AM_SHORT,   2 },
  { hidx_rank_medlonghandler, rank_medlonghandler, GEX_FLAG_AM_REQUEST|GEX_FLAG_AM_MEDLONG, 2 }
 };
#define HANDLER_TABLE_SIZE (sizeof(htable)/sizeof(gex_AM_Entry_t))

// Odds-in-row team (exercise new_tmp_p = NULL case):
static gex_TM_t oddtm;
static void *odd_scratch;
static size_t odd_scratch_sz;
static void do_odds(void) {
  oddtm = rowtm; // init just to check whether overwritten
  int odd = myrank & 1;
  gex_TM_t *new_tm_p = odd ? &oddtm : NULL;
  gex_TM_Split(new_tm_p, rowtm, 0, 0, odd_scratch, odd_scratch_sz, 0);
  if (odd) {
    assert_always(oddtm != rowtm);
    gex_Rank_t size = gex_TM_QuerySize(oddtm);
    assert_always(size <= gex_TM_QuerySize(rowtm));
    // Check that tie-breaks on key==0 respect order in parent team.
    // Taking a short-cut here knowning parent (rowtm) is in jobrank order and contiguous.
    gex_Rank_t first = gex_TM_TranslateRankToJobrank(oddtm, 0);
    for (gex_Rank_t rank = 1; rank < size; ++rank) {
      gex_Rank_t jobrank = gex_TM_TranslateRankToJobrank(oddtm, rank);
      assert_always(jobrank == first + 2*rank);
    }
    // Check gex_TM_TranslateJobrankToRank() for a guaranteed non-member
    assert_always(GEX_RANK_INVALID == gex_TM_TranslateJobrankToRank(oddtm,0));
  } else {
    assert_always(oddtm == rowtm); // Should be unchanged
  }
}

// Evens only team (exercise Create)
static gex_TM_t eventm;
static void *even_scratch;
static size_t even_scratch_sz;
static void do_evens(void) {
  static int reps = 0;
  eventm = coltm; // init just to check whether overwritten
  int even = !(myrank & 1);
  gex_Rank_t nmembers = even ? (gex_TM_QuerySize(myteam) + 1)/2 : 0;
  gex_EP_Location_t *members = test_calloc(sizeof(gex_EP_Location_t), nmembers);
  for (gex_Rank_t i = 0; i < nmembers; ++ i) members[i].gex_rank = i * 2;
  gex_Flags_t scratch_flag = (++reps & 1) ? GEX_FLAG_TM_LOCAL_SCRATCH : GEX_FLAG_TM_NO_SCRATCH;
  gex_TM_Create(&eventm, 1, myteam, members, nmembers, &even_scratch, even_scratch_sz, scratch_flag);
  if (even) {
    assert_always(eventm != coltm);
    assert_always(gex_TM_QuerySize(eventm) == nmembers);
    for (gex_Rank_t rank = 0; rank < nmembers; ++rank) {
      gex_Rank_t jobrank = gex_TM_TranslateRankToJobrank(eventm, rank);
      assert_always(jobrank == 2*rank);
    }
  } else {
    assert_always(eventm == coltm); // Should be unchanged
  }
  test_free(members);
}

static void *threadmain(void *id) {
  if (id) {
    do_evens();
  } else {
    do_odds();
  }
  return NULL;
}

int main(int argc, char **argv)
{
  gex_Rank_t peer;

  GASNET_Safe(gex_Client_Init(&myclient, &myep, &myteam, "testsplit", &argc, &argv, 0));

  test_init("testsplit", 0, "(nrows) (ncols)");

  myrank = gex_TM_QueryRank(myteam);
  gex_Rank_t nranks = gex_TM_QuerySize(myteam);

  gex_Rank_t nrows;
  if (argc > 1) {
    nrows = atoi(argv[1]);
  } else {
    /* search for as near to square as possible */
    nrows = sqrt(nranks);
    while (nranks % nrows) --nrows;
  }

  gex_Rank_t ncols;
  if (argc > 2) {
    ncols = atoi(argv[2]);
  } else {
    ncols = nranks / nrows;
  }
  assert_always(nrows*ncols == nranks);

  gex_Rank_t myrow = myrank / ncols;
  gex_Rank_t mycol = myrank % ncols;

  MSG0("Running split test with a %u-by-%u grid.", (int)nrows, (int)ncols);

  GASNET_Safe(gex_Segment_Attach(&mysegment, myteam, TEST_SEGSZ_REQUEST));
  BARRIER();

  // Will reserve all but first page of segment for scratch space
  scratch_addr = PAGESZ + (uintptr_t)TEST_MYSEG();
  scratch_end = TEST_SEGSZ + (uintptr_t)TEST_MYSEG();
  size_t scratch_sz;

  // Spec says NULL new_tm_p returns zero.
  scratch_sz = gex_TM_Split(NULL, myteam, 0, 1, 0, 0, GEX_FLAG_TM_SCRATCH_SIZE_RECOMMENDED);
  assert_always(scratch_sz == 0);

  // Row team:
  rowtm = myteam; // init just to check whether overwritten
  scratch_sz = gex_TM_Split(&rowtm, myteam, myrow, 1+2*mycol, 0, 0, SCRATCH_QUERY_FLAG);
  assert_always((scratch_addr + scratch_sz) <= scratch_end);
  gex_TM_Split(&rowtm, myteam, myrow, 1+2*mycol, (void*)scratch_addr, scratch_sz, 0);
  scratch_addr += scratch_sz;
  assert_always(rowtm != myteam);
  assert_always(gex_TM_QueryRank(rowtm) == mycol);
  assert_always(gex_TM_QuerySize(rowtm) == ncols);
  for (gex_Rank_t rank = 0; rank < ncols; ++rank) {
    gex_Rank_t jobrank = myrow*ncols + rank;
    assert_always(gex_TM_TranslateRankToJobrank(rowtm, rank) == jobrank);
    assert_always(gex_TM_TranslateJobrankToRank(rowtm, jobrank) == rank);
    gex_EP_Location_t ep_loc = gex_TM_TranslateRankToEP(rowtm, rank, 0);
    assert_always(ep_loc.gex_rank     == jobrank);
    assert_always(ep_loc.gex_ep_index == 0);
  }

  // Column team:
  coltm = myteam; // init just to check whether overwritten
  scratch_sz = gex_TM_Split(&coltm, myteam, mycol, myrow, 0, 0, SCRATCH_QUERY_FLAG);
  assert_always((scratch_addr + scratch_sz) <= scratch_end);
  gex_TM_Split(&coltm, myteam, mycol, myrow, (void*)scratch_addr, scratch_sz, 0);
  scratch_addr += scratch_sz;
  assert_always(coltm != myteam);
  assert_always(gex_TM_QueryRank(coltm) == myrow);
  assert_always(gex_TM_QuerySize(coltm) == nrows);
  for (gex_Rank_t rank = 0; rank < nrows; ++rank) {
    gex_Rank_t jobrank = mycol + ncols*rank;
    assert_always(gex_TM_TranslateRankToJobrank(coltm, rank) == jobrank);
    assert_always(gex_TM_TranslateJobrankToRank(coltm, jobrank) == rank);
    gex_EP_Location_t ep_loc = gex_TM_TranslateRankToEP(coltm, rank, 0);
    assert_always(ep_loc.gex_rank     == jobrank);
    assert_always(ep_loc.gex_ep_index == 0);
  }

  // Singleton team (also tests a 2nd-level split, of coltm, and GEX_FLAG_TM_NO_SCRATCH):
  gex_TM_t onetm = coltm; // init just to check whether overwritten
  gex_TM_Split(&onetm, coltm, myrank, 0, NULL, 0, GEX_FLAG_TM_NO_SCRATCH);
  assert_always(onetm != coltm);
  assert_always(gex_TM_QueryRank(onetm) == 0);
  assert_always(gex_TM_QuerySize(onetm) == 1);
  assert_always(gex_TM_TranslateRankToJobrank(onetm, 0) == myrank);
  assert_always(gex_TM_TranslateJobrankToRank(onetm, myrank) == 0);

  // Odds team tests
  odd_scratch = (void*)scratch_addr;
  odd_scratch_sz = gex_TM_Split((myrank & 1) ? &oddtm : NULL, rowtm, 0, 0, 0, 0, SCRATCH_QUERY_FLAG);
  assert_always((scratch_addr + odd_scratch_sz) <= scratch_end);
  scratch_addr += odd_scratch_sz;
  do_odds();

  // Evens team test
  even_scratch = (void*)scratch_addr;
  even_scratch_sz = gex_TM_Create(NULL, 1, myteam, NULL, myrank & 1 ? 0 : (nranks+1)/2, NULL, 0, SCRATCH_QUERY_FLAG);
  assert_always((scratch_addr + even_scratch_sz) <= scratch_end);
  scratch_addr += even_scratch_sz;
  do_evens();

  // "Rev" team reversing order of TM0
  gex_TM_t revtm = myteam; // init just to check whether overwritten
  const int alpha = 7, beta = 4;
  int mykey = alpha + beta * (nranks - (myrank + 1));
  scratch_sz = gex_TM_Split(&revtm, myteam, 0xf00, mykey, 0, 0, SCRATCH_QUERY_FLAG);
  assert_always((scratch_addr + scratch_sz) <= scratch_end);
  gex_TM_Split(&revtm, myteam, 0xf00, mykey, (void*)scratch_addr, scratch_sz, 0);
  scratch_addr += scratch_sz;
  assert_always(revtm != myteam);
  assert_always(gex_TM_QuerySize(revtm) == nranks);
  assert_always(gex_TM_QueryRank(revtm) == (nranks - (myrank + 1)));
 
  //
  // Some basic validation by communicating w/i the new teams
  //

  gex_Rank_t rank_var, *rank_ptr;
  gex_Rank_t *rank_arr = TEST_MYSEG();
  rank_arr[0] = myrank;
  rank_arr[1] = GEX_RANK_INVALID;
  rank_arr[2] = GEX_RANK_INVALID;
  BARRIER();

  // Try loopback Gets N ways:
  rank_ptr = TEST_MYSEG();
  rank_var = gex_RMA_GetBlockingVal(myteam, myrank, rank_ptr, sizeof(gex_Rank_t), 0);
  assert_always(rank_var == myrank);
  rank_var = GEX_RANK_INVALID;
  gex_RMA_GetBlocking(onetm, &rank_var, 0, rank_ptr, sizeof(gex_Rank_t), 0);
  assert_always(rank_var == myrank);
  rank_var = GEX_RANK_INVALID;
  gex_Event_Wait(gex_RMA_GetNB(rowtm, &rank_var, mycol, rank_ptr, sizeof(gex_Rank_t), 0));
  assert_always(rank_var == myrank);
  rank_var = GEX_RANK_INVALID;
  gex_RMA_GetNBI(coltm, &rank_var, myrow, rank_ptr, sizeof(gex_Rank_t), 0);
  gex_NBI_Wait(GEX_EC_GET, 0);
  assert_always(rank_var == myrank);
  BARRIER();

  // Blocking loop-back Put on singleton team
  peer = 0;
  rank_ptr = (gex_Rank_t *)TEST_SEG_TM(onetm, peer);
  assert_always(rank_ptr == rank_arr);
  rank_var = myrank ^ 42;
  gex_RMA_PutBlocking(onetm, 0, rank_ptr, &rank_var, sizeof(rank_var), 0);
  assert_always(rank_arr[0] == rank_var);

  // PutNB on row ring
  peer = (mycol+1)%ncols; // mycol and ncols are position in, and length of, the row
  rank_ptr = (gex_Rank_t *)TEST_SEG_TM(rowtm, peer);
  gex_Event_Wait(gex_RMA_PutNB(rowtm, peer, rank_ptr+1, &myrank, sizeof(myrank), GEX_EVENT_NOW, 0));
  gex_Event_Wait(gex_Coll_BarrierNB(rowtm, 0));
  assert_always(rank_arr[1] == gex_TM_TranslateRankToJobrank(rowtm, (mycol+ncols-1)%ncols));

  // PutNBI on col ring
  peer = (myrow+1)%nrows; // myrow and nrows are position in, and length of, the col
  rank_ptr = (gex_Rank_t *)TEST_SEG_TM(coltm, peer);
  gex_RMA_PutNBI(coltm, peer, rank_ptr+2, &myrank, sizeof(myrank), GEX_EVENT_NOW, 0);
  gex_NBI_Wait(GEX_EC_GET, 0);
  gex_Event_Wait(gex_Coll_BarrierNB(coltm, 0));
  assert_always(rank_arr[2] == gex_TM_TranslateRankToJobrank(coltm, (myrow+nrows-1)%nrows));

  // AM tests
  GASNET_Safe(gex_EP_RegisterHandlers(myep, htable, sizeof(htable)/sizeof(gex_AM_Entry_t)));
  BARRIER();
  peer = (mycol+1)%ncols;
  gex_AM_RequestShort2(rowtm, peer, hidx_rank_shorthandler, 0,
                       (gex_AM_Arg_t)gex_TM_TranslateRankToJobrank(rowtm, peer),
                       (gex_AM_Arg_t)myrank);
  peer = (myrow+1)%nrows;
  gex_AM_RequestMedium2(coltm, peer, hidx_rank_medlonghandler,
                        NULL, 0, GEX_EVENT_NOW, 0,
                        (gex_AM_Arg_t)gex_TM_TranslateRankToJobrank(coltm, peer),
                        (gex_AM_Arg_t)myrank);
  gex_AM_RequestLong2(onetm, 0, hidx_rank_medlonghandler,
                      NULL, 0, TEST_MYSEG(), GEX_EVENT_NOW, 0,
                      (gex_AM_Arg_t)myrank, (gex_AM_Arg_t)myrank);
  GASNET_BLOCKUNTIL(gasnett_atomic_read(&am_cntr,0) == 3);
  BARRIER();

  // Barrier over evens or odds (to exercise them) and then destroy
  {
    gex_TM_t tm = (myrank & 1) ? oddtm : eventm;
    gex_Event_Wait(gex_Coll_BarrierNB(tm, 0));
    gex_Memvec_t scratch_out;
    int rc = gex_TM_Destroy(tm, &scratch_out, GEX_FLAG_GLOBALLY_QUIESCED);
    assert_always(rc);
    assert_always(scratch_out.gex_addr == (void*)((myrank & 1) ? odd_scratch : even_scratch));
    assert_always(scratch_out.gex_len == ((myrank & 1) ? odd_scratch_sz : even_scratch_sz));
  }

  // REcreate and REdestroy repeatedly in an attempt to exhaust 12-bit space
  for (int i=0; i<4096; ++i) {
    do_evens();
    do_odds();
    assert_always(! gex_TM_Destroy((myrank & 1) ? oddtm : eventm, NULL, 0));
  }

  // More destruction
  {
    // NO_SCRATCH case must not write to *scratch_p
    gex_Memvec_t scratch_out;
    scratch_out.gex_addr = (void*)&myclient;
    scratch_out.gex_len  = myrank;
    assert_always(! gex_TM_Destroy(onetm, &scratch_out, 0));
    assert_always(scratch_out.gex_addr == (void*)&myclient);
    assert_always(scratch_out.gex_len  == myrank);
  }
  assert_always(! gex_TM_Destroy(rowtm, NULL, 0));
  assert_always(! gex_TM_Destroy(coltm, NULL, 0));
  assert_always(! gex_TM_Destroy(revtm, NULL, 0));

  MSG("done.");

  gasnet_exit(0); /* for faster exit */
  return 0;
}
