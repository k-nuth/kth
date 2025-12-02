


void lmdb_resized(KTH_DB_env *env)
{
  mdb_txn_safe::prevent_new_txns();
  MGINFO("LMDB map resize detected.");
  MDB_envinfo mei;
  mdb_env_info(env, &mei);
  uint64_t old = mei.me_mapsize;
  mdb_txn_safe::wait_no_active_txns();
  int result = kth_db_env_set_mapsize(env, 0);
  if (result)
    throw0(DB_ERROR(lmdb_error("Failed to set new mapsize: ", result).c_str()));
  mdb_env_info(env, &mei);
  uint64_t new_mapsize = mei.me_mapsize;
  MGINFO("LMDB Mapsize increased." << "  Old: " << old / (1024 * 1024) << "MiB" << ", New: " << new_mapsize / (1024 * 1024) << "MiB");
  mdb_txn_safe::allow_new_txns();
}
inline int lmdb_txn_begin(KTH_DB_env *env, KTH_DB_txn *parent, unsigned int flags, KTH_DB_txn **txn)
{
  int res = kth_db_txn_begin(env, parent, flags, txn);
  if (res == MDB_MAP_RESIZED) {
    lmdb_resized(env);
    res = kth_db_txn_begin(env, parent, flags, txn);
  }
  return res;
}
void BlockchainLMDB::do_resize(uint64_t increase_size)
{
  LOG_PRINT_L3("BlockchainLMDB::" << __func__);
  CRITICAL_REGION_LOCAL(m_synchronization_lock);
  constexpr uint64_t add_size = 1LL << 30;
  // check disk capacity
  try {
    kth::path path(m_folder);
    std::filesystem::space_info si = std::filesystem::space(path);
    if(si.available < add_size) {
      MERROR("!! WARNING: Insufficient free space to extend database !!: " <<
          (si.available >> 20L) << " MB available, " << (add_size >> 20L) << " MB needed");
      return;
    }
  }
  catch(...)
  {
    // print something but proceed.
    MWARNING("Unable to query free disk space.");
  }
  MDB_envinfo mei;
  mdb_env_info(m_env, &mei);
  MDB_stat mst;
  mdb_env_stat(m_env, &mst);
  // add 1Gb per resize, instead of doing a percentage increase
  uint64_t new_mapsize = (double) mei.me_mapsize + add_size;
  // If given, use increase_size instead of above way of resizing.
  // This is currently used for increasing by an estimated size at start of new
  // batch txn.
  if (increase_size > 0)
    new_mapsize = mei.me_mapsize + increase_size;
  new_mapsize += (new_mapsize % mst.ms_psize);
  mdb_txn_safe::prevent_new_txns();
  if (m_write_txn != nullptr)
  {
    if (m_batch_active) {
      throw0(DB_ERROR("lmdb resizing not yet supported when batch transactions enabled!"));
    }
    else
    {
      throw0(DB_ERROR("attempting resize with write transaction in progress, this should not happen!"));
    }
  }
  mdb_txn_safe::wait_no_active_txns();
  int result = kth_db_env_set_mapsize(m_env, new_mapsize);
  if (result)
    throw0(DB_ERROR(lmdb_error("Failed to set new mapsize: ", result).c_str()));
  MGINFO("LMDB Mapsize increased." << "  Old: " << mei.me_mapsize / (1024 * 1024) << "MiB" << ", New: " << new_mapsize / (1024 * 1024) << "MiB");
  mdb_txn_safe::allow_new_txns();
}
// threshold_size is used for batch transactions
bool BlockchainLMDB::need_resize(uint64_t threshold_size) const {
  LOG_PRINT_L3("BlockchainLMDB::" << __func__);
#if defined(ENABLE_AUTO_RESIZE)
  MDB_envinfo mei;
  mdb_env_info(m_env, &mei);
  MDB_stat mst;
  mdb_env_stat(m_env, &mst);
  // size_used doesn't include data yet to be committed, which can be
  // significant size during batch transactions. For that, we estimate the size
  // needed at the beginning of the batch transaction and pass in the
  // additional size needed.
  uint64_t size_used = mst.ms_psize * mei.me_last_pgno;
  LOG_PRINT_L1("DB map size:     " << mei.me_mapsize);
  LOG_PRINT_L1("Space used:      " << size_used);
  LOG_PRINT_L1("Space remaining: " << mei.me_mapsize - size_used);
  LOG_PRINT_L1("Size threshold:  " << threshold_size);
  float resize_percent = RESIZE_PERCENT;
  LOG_PRINT_L1(boost::format("Percent used: %.04f  Percent threshold: %.04f") % ((double)size_used/mei.me_mapsize) % resize_percent);
  if (threshold_size > 0)
  {
    if (mei.me_mapsize - size_used < threshold_size) {
      LOG_PRINT_L1("Threshold met (size-based)");
      return true;
    }
    else
      return false;
  }
  if ((double)size_used / mei.me_mapsize  > resize_percent)
  {
    LOG_PRINT_L1("Threshold met (percent-based)");
    return true;
  }
  return false;
#else
  return false;
#endif
}
void BlockchainLMDB::check_and_resize_for_batch(uint64_t batch_num_blocks, uint64_t batch_bytes)
{
  LOG_PRINT_L3("BlockchainLMDB::" << __func__);
  LOG_PRINT_L1("[" << __func__ << "] " << "checking DB size");
  constexpr uint64_t min_increase_size = 512 * (1 << 20);
  uint64_t threshold_size = 0;
  uint64_t increase_size = 0;
  if (batch_num_blocks > 0)
  {
    threshold_size = get_estimated_batch_size(batch_num_blocks, batch_bytes);
    MDEBUG("calculated batch size: " << threshold_size);
    // The increased DB size could be a multiple of threshold_size, a fixed
    // size increase (> threshold_size), or other variations.
    //
    // Currently we use the greater of threshold size and a minimum size. The
    // minimum size increase is used to avoid frequent resizes when the batch
    // size is set to a very small numbers of blocks.
    increase_size = (threshold_size > min_increase_size) ? threshold_size : min_increase_size;
    MDEBUG("increase size: " << increase_size);
  }
  // if threshold_size is 0 (i.e. number of blocks for batch not passed in), it
  // will fall back to the percent-based threshold check instead of the
  // size-based check
  if (need_resize(threshold_size))
  {
    MGINFO("[batch] DB resize needed");
    do_resize(increase_size);
  }
}
uint64_t BlockchainLMDB::get_estimated_batch_size(uint64_t batch_num_blocks, uint64_t batch_bytes) const {
  LOG_PRINT_L3("BlockchainLMDB::" << __func__);
  uint64_t threshold_size = 0;
  // batch size estimate * batch safety factor = final size estimate
  // Takes into account "reasonable" block size increases in batch.
  float batch_safety_factor = 1.7f;
  float batch_fudge_factor = batch_safety_factor * batch_num_blocks;
  // estimate of stored block expanded from raw block, including denormalization and db overhead.
  // Note that this probably doesn't grow linearly with block size.
  float db_expand_factor = 4.5f;
  uint64_t num_prev_blocks = 500;
  // For resizing purposes, allow for at least 4k average block size.
  uint64_t min_block_size = 4 * 1024;
  uint64_t block_stop = 0;
  uint64_t m_height = height();
  if (m_height > 1)
    block_stop = m_height - 1;
  uint64_t block_start = 0;
  if (block_stop >= num_prev_blocks)
    block_start = block_stop - num_prev_blocks + 1;
  uint32_t num_blocks_used = 0;
  uint64_t total_block_size = 0;
  MDEBUG("[" << __func__ << "] " << "m_height: " << m_height << "  block_start: " << block_start << "  block_stop: " << block_stop);
  size_t avg_block_size = 0;
  if (batch_bytes)
  {
    avg_block_size = batch_bytes / batch_num_blocks;
    goto estim;
  }
  if (m_height == 0)
  {
    MDEBUG("No existing blocks to check for average block size");
  }
  else if (m_cum_count >= num_prev_blocks)
  {
    avg_block_size = m_cum_size / m_cum_count;
    MDEBUG("average block size across recent " << m_cum_count << " blocks: " << avg_block_size);
    m_cum_size = 0;
    m_cum_count = 0;
  }
  else
  {
    KTH_DB_txn *rtxn;
    mdb_txn_cursors *rcurs;
    block_rtxn_start(&rtxn, &rcurs);
    for (uint64_t block_num = block_start; block_num <= block_stop; ++block_num) {
      // we have access to block weight, which will be greater or equal to block size,
      // so use this as a proxy. If it's too much off, we might have to check actual size,
      // which involves reading more data, so is not really wanted
      size_t block_weight = get_block_weight(block_num);
      total_block_size += block_weight;
      // Track number of blocks being totalled here instead of assuming, in case
      // some blocks were to be skipped for being outliers.
      ++num_blocks_used;
    }
    block_rtxn_stop();
    avg_block_size = total_block_size / num_blocks_used;
    MDEBUG("average block size across recent " << num_blocks_used << " blocks: " << avg_block_size);
  }
estim:
  if (avg_block_size < min_block_size)
    avg_block_size = min_block_size;
  MDEBUG("estimated average block size for batch: " << avg_block_size);
  // bigger safety margin on smaller block sizes
  if (batch_fudge_factor < 5000.0)
    batch_fudge_factor = 5000.0;
  threshold_size = avg_block_size * db_expand_factor * batch_fudge_factor;
  return threshold_size;
}


void BlockchainLMDB::open(std::string const& filename, int const db_flags)
{
  int result;
  int mdb_flags = KTH_DB_NORDAHEAD;

  LOG_PRINT_L3("BlockchainLMDB::" << __func__);

  if (m_open)
    throw0(DB_OPEN_FAILURE("Attempted to open db, but it's already open"));

  kth::path direc(filename);
  if (std::filesystem::exists(direc))
  {
    if ( ! std::filesystem::is_directory(direc))
      throw0(DB_OPEN_FAILURE("LMDB needs a directory path, but a file was passed"));
  }
  else
  {
    if ( ! std::filesystem::create_directories(direc))
      throw0(DB_OPEN_FAILURE(std::string("Failed to create directory ").append(filename).c_str()));
  }

  // check for existing LMDB files in base directory
  kth::path old_files = direc.parent_path();
  if (std::filesystem::exists(old_files / CRYPTONOTE_BLOCKCHAINDATA_FILENAME)
      || std::filesystem::exists(old_files / CRYPTONOTE_BLOCKCHAINDATA_LOCK_FILENAME))
  {
    LOG_PRINT_L0("Found existing LMDB files in " << old_files.string());
    LOG_PRINT_L0("Move " << CRYPTONOTE_BLOCKCHAINDATA_FILENAME << " and/or " << CRYPTONOTE_BLOCKCHAINDATA_LOCK_FILENAME << " to " << filename << ", or delete them, and then restart");
    throw DB_ERROR("Database could not be opened");
  }

  std::optional<bool> is_hdd_result = tools::is_hdd(filename.c_str());
  if (is_hdd_result)
  {
    if (is_hdd_result.value())
        MCLOG_RED(el::Level::Warning, "global", "The blockchain is on a rotating drive: this will be very slow, use a SSD if possible");
  }

  m_folder = filename;

#ifdef __OpenBSD__
  if ((mdb_flags & KTH_DB_WRITEMAP) == 0) {
    MCLOG_RED(el::Level::Info, "global", "Running on OpenBSD: forcing WRITEMAP");
    mdb_flags |= KTH_DB_WRITEMAP;
  }
#endif
  // set up lmdb environment
  if ((result = kth_db_env_create(&m_env)))
    throw0(DB_ERROR(lmdb_error("Failed to create lmdb environment: ", result).c_str()));
  if ((result = kth_db_env_set_maxdbs(m_env, 20)))
    throw0(DB_ERROR(lmdb_error("Failed to set max number of dbs: ", result).c_str()));

  int threads = tools::get_max_concurrency();
  if (threads > 110 &&	/* maxreaders default is 126, leave some slots for other read processes */
    (result = mdb_env_set_maxreaders(m_env, threads+16)))
    throw0(DB_ERROR(lmdb_error("Failed to set max number of readers: ", result).c_str()));

  size_t mapsize = DEFAULT_MAPSIZE;

  if (db_flags & DBF_FAST)
    mdb_flags |= KTH_DB_NOSYNC;
  if (db_flags & DBF_FASTEST)
    mdb_flags |= KTH_DB_NOSYNC | KTH_DB_WRITEMAP | KTH_DB_MAPASYNC;
  if (db_flags & DBF_RDONLY)
    mdb_flags = KTH_DB_RDONLY;
  if (db_flags & DBF_SALVAGE)
    mdb_flags |= MDB_PREVSNAPSHOT;

  if (auto result = kth_db_env_open(m_env, filename.c_str(), mdb_flags, 0644))
    throw0(DB_ERROR(lmdb_error("Failed to open lmdb environment: ", result).c_str()));

  MDB_envinfo mei;
  mdb_env_info(m_env, &mei);
  uint64_t cur_mapsize = (double)mei.me_mapsize;

  if (cur_mapsize < mapsize)
  {
    if (auto result = kth_db_env_set_mapsize(m_env, mapsize))
      throw0(DB_ERROR(lmdb_error("Failed to set max memory map size: ", result).c_str()));
    mdb_env_info(m_env, &mei);
    cur_mapsize = (double)mei.me_mapsize;
    LOG_PRINT_L1("LMDB memory map size: " << cur_mapsize);
  }

  if (need_resize())
  {
    LOG_PRINT_L0("LMDB memory map needs to be resized, doing that now.");
    do_resize();
  }

  int txn_flags = 0;
  if (mdb_flags & KTH_DB_RDONLY)
    txn_flags |= KTH_DB_RDONLY;

  // get a read/write KTH_DB_txn, depending on mdb_flags
  mdb_txn_safe txn;
  if (auto mdb_res = kth_db_txn_begin(m_env, NULL, txn_flags, txn))
    throw0(DB_ERROR(lmdb_error("Failed to create a transaction for the db: ", mdb_res).c_str()));

  // open necessary databases, and set properties as needed
  // uses macros to avoid having to change things too many places
  lmdb_db_open(txn, LMDB_BLOCKS, KTH_DB_INTEGERKEY | KTH_DB_CREATE, m_blocks, "Failed to open db handle for m_blocks");

  lmdb_db_open(txn, LMDB_BLOCK_INFO, KTH_DB_INTEGERKEY | KTH_DB_CREATE | KTH_DB_DUPSORT | KTH_DB_DUPFIXED, m_block_info, "Failed to open db handle for m_block_info");
  lmdb_db_open(txn, LMDB_BLOCK_HEIGHTS, KTH_DB_INTEGERKEY | KTH_DB_CREATE | KTH_DB_DUPSORT | KTH_DB_DUPFIXED, m_block_heights, "Failed to open db handle for m_block_heights");

  lmdb_db_open(txn, LMDB_TXS, KTH_DB_INTEGERKEY | KTH_DB_CREATE, m_txs, "Failed to open db handle for m_txs");
  lmdb_db_open(txn, LMDB_TXS_PRUNED, KTH_DB_INTEGERKEY | KTH_DB_CREATE, m_txs_pruned, "Failed to open db handle for m_txs_pruned");
  lmdb_db_open(txn, LMDB_TXS_PRUNABLE, KTH_DB_INTEGERKEY | KTH_DB_CREATE, m_txs_prunable, "Failed to open db handle for m_txs_prunable");
  lmdb_db_open(txn, LMDB_TXS_PRUNABLE_HASH, KTH_DB_INTEGERKEY | KTH_DB_CREATE, m_txs_prunable_hash, "Failed to open db handle for m_txs_prunable_hash");
  lmdb_db_open(txn, LMDB_TX_INDICES, KTH_DB_INTEGERKEY | KTH_DB_CREATE | KTH_DB_DUPSORT | KTH_DB_DUPFIXED, m_tx_indices, "Failed to open db handle for m_tx_indices");
  lmdb_db_open(txn, LMDB_TX_OUTPUTS, KTH_DB_INTEGERKEY | KTH_DB_CREATE, m_tx_outputs, "Failed to open db handle for m_tx_outputs");

  lmdb_db_open(txn, LMDB_OUTPUT_TXS, KTH_DB_INTEGERKEY | KTH_DB_CREATE | KTH_DB_DUPSORT | KTH_DB_DUPFIXED, m_output_txs, "Failed to open db handle for m_output_txs");
  lmdb_db_open(txn, LMDB_OUTPUT_AMOUNTS, KTH_DB_INTEGERKEY | KTH_DB_DUPSORT | KTH_DB_DUPFIXED | KTH_DB_CREATE, m_output_amounts, "Failed to open db handle for m_output_amounts");

  lmdb_db_open(txn, LMDB_SPENT_KEYS, KTH_DB_INTEGERKEY | KTH_DB_CREATE | KTH_DB_DUPSORT | KTH_DB_DUPFIXED, m_spent_keys, "Failed to open db handle for m_spent_keys");

  lmdb_db_open(txn, LMDB_TXPOOL_META, KTH_DB_CREATE, m_txpool_meta, "Failed to open db handle for m_txpool_meta");
  lmdb_db_open(txn, LMDB_TXPOOL_BLOB, KTH_DB_CREATE, m_txpool_blob, "Failed to open db handle for m_txpool_blob");

  // this subdb is dropped on sight, so it may not be present when we open the DB.
  // Since we use KTH_DB_CREATE, we'll get an exception if we open read-only and it does not exist.
  // So we don't open for read-only, and also not drop below. It is not used elsewhere.
  if ( ! (mdb_flags & KTH_DB_RDONLY))
    lmdb_db_open(txn, LMDB_HF_STARTING_HEIGHTS, KTH_DB_CREATE, m_hf_starting_heights, "Failed to open db handle for m_hf_starting_heights");

  lmdb_db_open(txn, LMDB_HF_VERSIONS, KTH_DB_INTEGERKEY | KTH_DB_CREATE, m_hf_versions, "Failed to open db handle for m_hf_versions");

  lmdb_db_open(txn, LMDB_PROPERTIES, KTH_DB_CREATE, m_properties, "Failed to open db handle for m_properties");

  mdb_set_dupsort(txn, m_spent_keys, compare_hash32);
  mdb_set_dupsort(txn, m_block_heights, compare_hash32);
  mdb_set_dupsort(txn, m_tx_indices, compare_hash32);
  mdb_set_dupsort(txn, m_output_amounts, compare_uint64);
  mdb_set_dupsort(txn, m_output_txs, compare_uint64);
  mdb_set_dupsort(txn, m_block_info, compare_uint64);

  mdb_set_compare(txn, m_txpool_meta, compare_hash32);
  mdb_set_compare(txn, m_txpool_blob, compare_hash32);
  mdb_set_compare(txn, m_properties, compare_string);

  if ( ! (mdb_flags & KTH_DB_RDONLY))
  {
    result = mdb_drop(txn, m_hf_starting_heights, 1);
    if (result && result != KTH_DB_NOTFOUND)
      throw0(DB_ERROR(lmdb_error("Failed to drop m_hf_starting_heights: ", result).c_str()));
  }

  // get and keep current height
  MDB_stat db_stats;
  if ((result = mdb_stat(txn, m_blocks, &db_stats)))
    throw0(DB_ERROR(lmdb_error("Failed to query m_blocks: ", result).c_str()));
  LOG_PRINT_L2("Setting m_height to: " << db_stats.ms_entries);
  uint64_t m_height = db_stats.ms_entries;

  bool compatible = true;

  MDB_val_copy<const char*> k("version");
  KTH_DB_val v;
  auto get_result = kth_db_get(txn, m_properties, &k, &v);
  if(get_result == KTH_DB_SUCCESS) {

    //TODO: check this cast
    uint32_t const db_version = *(uint32_t const*) kth_db_get_data(v);
    if (db_version > VERSION) {
      MWARNING("Existing lmdb database was made by a later version (" << db_version << "). We don't know how it will change yet.");
      compatible = false;
    }
#if VERSION > 0
    else if (db_version < VERSION) {
      // Note that there was a schema change within version 0 as well.
      // See commit e5d2680094ee15889934fe28901e4e133cda56f2 2015/07/10
      // We don't handle the old format previous to that commit.
      txn.commit();
      m_open = true;
      migrate(db_version);
      return;
    }
#endif
  }
  else
  {
    // if not found, and the DB is non-empty, this is probably
    // an "old" version 0, which we don't handle. If the DB is
    // empty it's fine.
    if (VERSION > 0 && m_height > 0)
      compatible = false;
  }

  if ( ! compatible)
  {
    txn.abort();
    kth_db_env_close(m_env);
    m_open = false;
    MFATAL("Existing lmdb database is incompatible with this version.");
    MFATAL("Please delete the existing database and resync.");
    return;
  }

  if ( ! (mdb_flags & KTH_DB_RDONLY))
  {
    // only write version on an empty DB
    if (m_height == 0) {
      MDB_val_copy<const char*> k("version");
      MDB_val_copy<uint32_t> v(VERSION);
      auto put_result = kth_db_put(txn, m_properties, &k, &v, 0);
      if (put_result != KTH_DB_SUCCESS)
      {
        txn.abort();
        kth_db_env_close(m_env);
        m_open = false;
        MERROR("Failed to write version to database.");
        return;
      }
    }
  }

  // commit the transaction
  txn.commit();

  m_open = true;
  // from here, init should be finished
}

void BlockchainLMDB::close()
{
  LOG_PRINT_L3("BlockchainLMDB::" << __func__);
  if (m_batch_active)
  {
    LOG_PRINT_L3("close() first calling batch_abort() due to active batch transaction");
    batch_abort();
  }
  this->sync();
  m_tinfo.reset();

  // FIXME: not yet thread safe!!!  Use with care.
  kth_db_env_close(m_env);
  m_open = false;
}

void BlockchainLMDB::safesyncmode(bool const onoff)
{
  MINFO("switching safe mode " << (onoff ? "on" : "off"));
  mdb_env_set_flags(m_env, KTH_DB_NOSYNC|KTH_DB_MAPASYNC, !onoff);
}

void BlockchainLMDB::reset()
{
  LOG_PRINT_L3("BlockchainLMDB::" << __func__);
  check_open();

  mdb_txn_safe txn;
  if (auto result = lmdb_txn_begin(m_env, NULL, 0, txn))
    throw0(DB_ERROR(lmdb_error("Failed to create a transaction for the db: ", result).c_str()));

  if (auto result = mdb_drop(txn, m_blocks, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_blocks: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_block_info, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_block_info: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_block_heights, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_block_heights: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_txs_pruned, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_txs_pruned: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_txs_prunable, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_txs_prunable: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_txs_prunable_hash, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_txs_prunable_hash: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_tx_indices, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_tx_indices: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_tx_outputs, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_tx_outputs: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_output_txs, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_output_txs: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_output_amounts, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_output_amounts: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_spent_keys, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_spent_keys: ", result).c_str()));
  (void)mdb_drop(txn, m_hf_starting_heights, 0); // this one is dropped in new code
  if (auto result = mdb_drop(txn, m_hf_versions, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_hf_versions: ", result).c_str()));
  if (auto result = mdb_drop(txn, m_properties, 0))
    throw0(DB_ERROR(lmdb_error("Failed to drop m_properties: ", result).c_str()));

  // init with current version
  MDB_val_copy<const char*> k("version");
  MDB_val_copy<uint32_t> v(VERSION);
  if (auto result = kth_db_put(txn, m_properties, &k, &v, 0))
    throw0(DB_ERROR(lmdb_error("Failed to write version to database: ", result).c_str()));

  txn.commit();
  m_cum_size = 0;
  m_cum_count = 0;
}



bool BlockchainLMDB::is_read_only() const {
  unsigned int flags;
  auto result = mdb_env_get_flags(m_env, &flags);
  if (result)
    throw0(DB_ERROR(lmdb_error("Error getting database environment info: ", result).c_str()));
  if (flags & KTH_DB_RDONLY)
    return true;
  return false;
}
uint64_t BlockchainLMDB::get_database_size() const {
  uint64_t size = 0;
  kth::path datafile(m_folder);
  datafile /= CRYPTONOTE_BLOCKCHAINDATA_FILENAME;
  if ( ! epee::file_io_utils::get_file_size(datafile.string(), size))
    size = 0;
  return size;
}



/*
Some Optimization
If you frequently begin and abort read-only transactions, as an optimization, it is possible to only reset and renew a transaction.
mdb_txn_reset() releases any old copies of data kept around for a read-only transaction. To reuse this reset transaction, call mdb_txn_renew() on it. 
Any cursors in this transaction must also be renewed using mdb_cursor_renew().
Note that mdb_txn_reset() is similar to kth_db_txn_abort() and will close any databases you opened within the transaction.
To permanently free a transaction, reset or not, use kth_db_txn_abort().
*/