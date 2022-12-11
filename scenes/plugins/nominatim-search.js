function nominatimSearch(query, bounds, flags)
{
  const url = "https://nominatim.openstreetmap.org/search?format=jsonv2&bounded=1&viewbox=" + bounds.join() + "&limit=50&q=" + query;
  jsonHttpRequest(url, "", function(content) {
    for(var ii = 0; ii < content.length; ii++) {
      const r = content[ii];
      const tags = {"name": r.display_name, [r.category]: r.type};
      addSearchResult(r.osm_id, r.lat, r.lon, r.importance, flags, tags);
    }
  });
}

registerFunction("nominatimSearch", "search", "Nominatim Search");
