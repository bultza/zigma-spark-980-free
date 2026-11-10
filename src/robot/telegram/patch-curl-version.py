#!/usr/bin/env python3
"""Apply our isolated Linux version-only entry point to an extracted curl tree."""
import pathlib,shutil,sys
root=pathlib.Path(__file__).resolve().parents[1];tree=pathlib.Path(sys.argv[1]);p=tree/'src/tool_main.c';s=p.read_text()
if 'zigma_version_only' not in s:
 s=s.replace('  CURLcode result = CURLE_OK;','''  /* -v alone is our version shortcut; -v with URLs keeps curl verbosity. */
#ifndef _WIN32
  if(zigma_version_only(argc, argv, "zigma-curl-arm")) {
    printf("%s\\n", curl_version());
    return 0;
  }
#endif
  CURLcode result = CURLE_OK;''')
 s=s.replace('#include "tool_setup.h"','#include "tool_setup.h"\n#include "zigma_version.h"')
 assert 'zigma_version_only' in s and '#include "zigma_version.h"' in s
 p.write_text(s)
for name in ['zigma_version.h','zigma_versions_generated.h']:shutil.copy2(root/'src'/name,tree/'src'/name)
