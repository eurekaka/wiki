* each stage is protected by mutex, i.e, LOCK_log, LOCK_sync etc, so there can be only one active leader for each stage;
  inside each stage, order is imposed; so sequence_number and last_committed is monotonically incremental; so sequence_number
  of transaction_ctx is end of locking interval, while transaction_ctx.last_committed is start of locking interval; so in slave,
  a transaction can execute in parallel if and only if its 'last_committed' is less than 'smallest sequence_number' of executing
  transactions(new comming transaction must be later than executing transactions);

  simplest way to implement locking interval is only using sequence_number; the reason for using last_committed as start of locking
  interval is to enlarge the locking interval(it is safe, because the enlarged interval are transactions in sync stage, so it is safe)

* logic of locking interval conflicting check on slave side is implemented in function schedule_next_event --> wait_for_last_committed_trx;
  if this transaction can execute in parallel with the current executing transactions, return; otherwise, wait for them to finish; the
  notification is implemented in slave_worker_ends_group when the waited transaction finishes;

* @sa worklog 7165 for details;
