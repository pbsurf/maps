function nominatimSearch(query, bounds, flags)
{
  const url = "https://nominatim.openstreetmap.org/search?format=jsonv2&bounded=1&viewbox=" + bounds.join() + "&limit=50&q=" + query;
  httpRequest(url, "", function(_content) {
    if(!_content) { notifyError("search", "Nominatim Search error"); return; }
    const content = JSON.parse(_content);
    if(content.length >= 50) { flags = flags | 0x8000; }  // MapSearch::MORE_RESULTS flag
    for(var ii = 0; ii < content.length; ii++) {
      const r = content[ii];
      const tags = {"name": r.display_name, [r.category]: r.type};
      addSearchResult(r.osm_id, r.lat, r.lon, r.importance, flags, tags);
    }
  });
}

registerFunction("nominatimSearch", "search", "Nominatim Search");
