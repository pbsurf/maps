//<html> <script type="text/javascript">

// Populate place info from OSM tags from API

// function addPlaceInfo(icon, title, value) { console.log(title + ": " + value); }
// function jsonHttpRequest(url, hdrs, callback) { fetch(url).then(res => callback(res.json())); }

// from https://github.com/osmlab/jsopeninghours
// - see https://wiki.openstreetmap.org/wiki/Key:opening_hours for more sophisticated (and far more complex) parsers
function parseOpeningHours(text)
{
  const days = ['mo', 'tu', 'we', 'th', 'fr', 'sa', 'su'];
  var result = null;
  if (text === '' || text === null) {
    result = null;
  }
  else if (text == '24/7') {
    result = {'24/7': true};
  }
  else if (text == 'seasonal') {
    result = {'seasonal': true};
  }
  else {
    result = {};
    var modified_some_days = false;
    for (var k = 0; k < days.length; k++) {
      result[k] = null;
    }

    var dayregex = /^(mo|tu|we|th|fr|sa|su)\-?(mo|tu|we|th|fr|sa|su)?$/,
      timeregex = /^\s*(\d\d:\d\d)\-(\d\d:\d\d)\s*$/,
      dayranges = text.toLowerCase().split(/\s*;\s*/),
      dayrange;
    while((dayrange = dayranges.shift())) {
      var daytimes = dayrange.trim().split(/\s+/),
        daytime,
        startday = 0,
        endday = 6,
        whichDays,
        whichTimes,
        starttime,
        endtime;

      while((daytime = daytimes.shift())) {
        if (dayregex.test(daytime)) {
          var daymatches = daytime.match(dayregex);

          if (daymatches.length === 3) {
            startday = days.indexOf(daymatches[1]);
            if (daymatches[2]) {
              endday = days.indexOf(daymatches[2]);
            } else {
              endday = startday;
            }
          } else {
            return null;
          }
        } else if (timeregex.test(daytime)) {
          var timematches = daytime.match(timeregex);

          if (timematches.length === 3) {
            starttime = timematches[1];
            endtime = timematches[2];
          } else {
            return null;
          }
        } else {
          return null;
        }
      }

      for (var j = startday; j <= endday; j++) {
        result[j] = [starttime, endtime];
        modified_some_days = true;
      }

      if (!modified_some_days) {
        result = null;
      }
    }
  }
  return result;
}
/*

How will we pass opening hours to app?
- we want row showing current state (closed until X or open until X)
- expanding down to hours for each day

- pass JS map to separate plugin fn?
- pass special string to special fn or addPlaceInfo

\n\n (or other delim) = fold; \n for each lin

How will user select which place info plugin to use?  support multiple plugins?
- show "More info from <select box for plugin, defaulting to first place info plugin>"


*/

function to12H(hm)
{
  const h = parseInt(hm);
  return h > 12 ? (h - 12) + hm.slice(2) + " PM" : hm + " AM";
}

function osmPlaceInfo(osmid)
{
  osmid = osmid.replace(":", "/");
  const url = "https://www.openstreetmap.org/api/0.6/" + osmid + ".json";
  jsonHttpRequest(url, "", function(content) {
    const tags = content["elements"][0]["tags"];

    if(tags["cuisine"]) {
      addPlaceInfo("food", "Cuisine", tags["cuisine"]);
    }
    //tags["takeaway"] (yes, no, only)
    //tags["outdoor_seating"] (yes, no)
    if(tags["addr:street"]) {
      const hnum = tags["addr:housenumber"];
      const city = tags["addr:city"];
      const state = tags["addr:state"] || tags["addr:province"];
      const zip = tags["addr:postcode"];
      const addr = (hnum ? (hnum + " ") : "") + street + (city ? " " + city : "")
          + (state ? ", " + state : "") + (zip ? " " + zip : "");
      addPlaceInfo("pin", "Address", addr);
    }
    if(tags["website"]) {
      addPlaceInfo("globe", "Website", tags["website"]);
    }
    if(tags["phone"]) {
      addPlaceInfo("phone", "Phone", tags["phone"]);
    }

    if(tags["opening_hours"]) {
      const days = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"];
      const hours = parseOpeningHours(tags["opening_hours"]);
      const now = new Date();
      const today = now.getDay();
      const nowhm = now.getHours() + ":" + now.getMinutes();
      const toolate = !hours[today] || hours[today][1] < nowhm;  // lexical comparison
      const tooearly = !toolate && hours[today][0] > nowhm;

      const state = tooearly || toolate ? "Closed" : "Open";
      if(tooearly) {
        state += " until " + to12H(hours[today][0]);
      } else if(toolate) {
        for (var ii = today; ii < today + 7; ii++) {
          if(hours[ii%7]) {
            state += " until " + days[ii%7] + " " + to12H(hours[ii%7][0]);
            break;
          }
        }
      }

      var daily = [];
      // TODO: use \t for alignment!
      for (var ii = 0; ii < hours.length; ii++) {
        daily.push( days[ii] + "  " + to12H(hours[ii][0]) + " - " + to12H(hours[ii][1]) );
      }

      addPlaceInfo("clock", "Hours", state + "\r" + daily.join("\n"));
    }
  });
}

registerFunction("osmPlaceInfo", "place", "OSM Place Info");

//</script> <head/> <body/> </html>
