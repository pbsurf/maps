// show all wikipedia entries in current map view

function lngLatDist(lng1, lat1, lng2, lat2)
{
  const p = 3.14159265358979323846/180;
  const a = 0.5 - Math.cos((lat2-lat1)*p)/2 + Math.cos(lat1*p) * Math.cos(lat2*p) * (1-Math.cos((lng2-lng1)*p))/2;
  return 12742 * Math.asin(Math.sqrt(a));  // kilometers
}

function wikipediaSearch(query, bounds, flags)
{
  // bounds provided as left,bottom,right,top; search by center and radius instead of bbox so we can order by dist
  const radkm = 0.5*lngLatDist(bounds[0], bounds[1], bounds[2], bounds[3]);
  const lng = (bounds[0] + bounds[2])/2;
  const lat = (bounds[1] + bounds[3])/2;

  if(radkm <= 10) {
    // much faster than sparql query but limited to 10km radius
    const url = "https://en.wikipedia.org/w/api.php?action=query&format=json&list=geosearch&gslimit=500&gsradius="
        + (radkm*1000).toFixed(0) + "&gscoord=" + [lat, lng].join('|');

    httpRequest(url, "", function(_content, _error) {
      if(!_content) { notifyError("search", "Wikipedia Search error"); return; }
      const content = JSON.parse(_content);
      const data = (content["query"] || {})["geosearch"] || [];
      if(data.length >= 500) { flags = flags | 0x8000; }  // MapSearch::MORE_RESULTS flag
      for(var ii = 0; ii < data.length; ii++) {
        const r = data[ii];
        const url = "https://en.wikipedia.org/?curid=" + r.pageid;
        const url_info = {"icon": "wikipedia", "title": "Wikipedia",
            "value": "<a href='" + url + "'><text>" + r.title + "</text></a>"};
        const tags = {"name": r.title, "place_info": [url_info]};
        addSearchResult(ii, r.lat, r.lon, data.length-ii, flags, tags);
      }
    });
  } else {
    const wikiGeoSparql = 'https://query.wikidata.org/sparql?query=' + encodeURI(
      'SELECT DISTINCT ?item ?itemLabel ?lng ?lat ?dist ?url' +
      'WHERE {' +
      '  ?url schema:about ?item;' +
      '       schema:inLanguage "en" .' +
      '  FILTER (STRSTARTS(str(?url), "https://en.wikipedia.org/")).' +
      '  SERVICE wikibase:around {' +
      '      ?item wdt:P625 ?where .' +
      '      bd:serviceParam wikibase:center "Point(' + lng + ' ' + lat + ')"^^geo:wktLiteral.' +
      '      bd:serviceParam wikibase:radius "' + radkm + '" . ' +
      '      bd:serviceParam wikibase:distance ?dist.' +
      '  }' +
      '  ?item p:P625 ?lnglat.' +
      '  ?lnglat psv:P625 ?lnglat_node.' +
      '  ?lnglat_node wikibase:geoLongitude ?lng.' +
      '  ?lnglat_node wikibase:geoLatitude ?lat.' +
      '  SERVICE wikibase:label { bd:serviceParam wikibase:language "en" . }' +
      '}' +
      'ORDER BY ASC(?dist)' +
      'LIMIT 1000"');

    httpRequest(url, "", function(_content, _error) {
      if(!_content) { notifyError("search", "Wikipedia Search error"); return; }
      const content = JSON.parse(_content);
      const data = (content["results"] || {})["bindings"] || [];
      if(data.length >= 1000) { flags = flags | 0x8000; }  // MapSearch::MORE_RESULTS flag
      for(var ii = 0; ii < data.length; ii++) {
        const r = data[ii];
        const url_info = {"icon": "wikipedia", "title": "Wikipedia",
            "value": "<a href='" + r.url.value + "'><text>" + r.itemLabel.value + "</text></a>"};
        const tags = {"name": r.itemLabel.value, "place_info": [url_info]};
        const lng = Number(r.lng.value), lat = Number(r.lat.value);
        addSearchResult(ii, lat, lng, radkm - Number(r.dist.value), flags, tags);
      }
    });
  }
}

registerFunction("wikipediaSearch", "search", "Wikipedia Search");
