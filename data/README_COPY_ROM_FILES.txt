ROM files are intentionally not redistributed in this project archive.
Copy the two original text-hex files without editing them:
  data/ROM6k
  data/ROMs0-C

From the project root:
  ./tools/copy_roms.sh /path/to/MCM70E_v2.1_distr

Then upload the full LittleFS data partition (ROMs plus any preloaded tape images):
  pio run -e m5-cardputer -t uploadfs

Review and comply with the separate ROM restrictions in MCM70E_software_license.
