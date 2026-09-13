# runners-journal

E-ink dashboard app for the Seeed reTerminal Sticky E1005. Fetches the
runner's journal and running stats from Supabase dashboard() RPC.

Setup: copy include/secrets.h.example to include/secrets.h, fill in WiFi
and Supabase anon key, then build with pio run -e reterminal_e1005.

License: GPL-2.0 (inherited from upstream danpodeanu/seeed-reterminal-E100X).
