/* ---- Defiant power widgets (battery packs, battery wall, solar, fuel) ----
   Values come from Signal K via AvNav's Signal K handler (nav.gps.signalk.*).
   Signal K units: stateOfCharge/currentLevel are 0-1 ratios, power in W. */
(function () {
    var SK = "nav.gps.signalk.";
    var PACKS = ["bank1a", "bank1b", "bank1c", "bank2a", "bank2b", "bank2c", "bank3-1", "bank3-2", "bank3-3"];

    var isNum = function (v) {
        return v !== undefined && v !== null && v !== "" && !isNaN(parseFloat(v));
    };
    var pct = function (ratio) {
        return isNum(ratio) ? Math.round(parseFloat(ratio) * 100) + "%" : "--";
    };
    // Positive power = charging, negative = discharging; under 1 W is idle.
    var flow = function (watts) {
        if (!isNum(watts)) return "<span class='defiantDim'>no data</span>";
        var w = parseFloat(watts);
        if (w >= 1) return "<span class='defiantChg'>&#9650; " + w.toFixed(0) + " W</span>";
        if (w <= -1) return "<span class='defiantDis'>&#9660; " + (-w).toFixed(0) + " W</span>";
        return "<span class='defiantDim'>idle</span>";
    };

    var ratioPercent = function (value) {
        return pct(value);
    };
    avnav.api.registerFormatter("defiantRatioPercent", ratioPercent);

    avnav.api.registerWidget({
        name: "DefiantBatteryPack",
        caption: "Battery",
        renderHtml: function (props) {
            return "<div class='widgetData defiantBig'>" + pct(props.soc) + "</div>" +
                "<div class='defiantSub'>" + flow(props.power) + "</div>";
        }
    }, {
        caption: true,
        soc: {type: "KEY", default: ""},
        power: {type: "KEY", default: ""}
    });

    var wallKeys = {};
    PACKS.forEach(function (p) {
        wallKeys["soc_" + p] = SK + "electrical.batteries." + p + ".capacity.stateOfCharge";
        wallKeys["pwr_" + p] = SK + "electrical.batteries." + p + ".power";
    });
    avnav.api.registerWidget({
        name: "DefiantBatteryWall",
        caption: "Battery Wall",
        storeKeys: wallKeys,
        renderHtml: function (props) {
            // Equal-capacity packs, so wall SoC is the mean of the packs reporting.
            var socSum = 0, n = 0, pwr = 0, pwrN = 0;
            PACKS.forEach(function (p) {
                var s = props["soc_" + p], w = props["pwr_" + p];
                if (isNum(s)) { socSum += parseFloat(s); n++; }
                if (isNum(w)) { pwr += parseFloat(w); pwrN++; }
            });
            var reporting = n === PACKS.length ? "" :
                "<div class='defiantWarn'>" + n + "/" + PACKS.length + " packs reporting</div>";
            return "<div class='widgetData defiantBig'>" + (n ? pct(socSum / n) : "--") + "</div>" +
                "<div class='defiantSub'>" + (pwrN ? flow(pwr) : flow()) + "</div>" + reporting;
        }
    }, {caption: true});

    var esc = function (s) {
        return String(s).replace(/[&<>"']/g, function (c) {
            return {"&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;"}[c];
        });
    };

    // The Venus plugin keys chargers by their Venus CustomName (camelCased), and
    // each one's side comes from that name ("Port ..." / "Starboard ...").
    var MPPTS = ["portBimini", "portDavits", "starboardBimini", "starboardDavits"];
    var solarKeys = {};
    MPPTS.forEach(function (id) {
        solarKeys["mppt_" + id] = SK + "electrical.solar." + id + ".panelPower";
        solarKeys["name_" + id] = SK + "electrical.solar." + id + ".name";
    });
    var sideOf = function (name) {
        var n = (name || "").toLowerCase();
        if (n.indexOf("port") === 0) return "port";
        if (n.indexOf("starboard") === 0 || n.indexOf("stbd") === 0) return "stbd";
        return null;
    };
    avnav.api.registerWidget({
        name: "DefiantSolar",
        caption: "Solar",
        storeKeys: solarKeys,
        renderHtml: function (props) {
            var total = 0, rows = "", side = {port: 0, stbd: 0}, unnamed = 0;
            MPPTS.forEach(function (id) {
                var w = props["mppt_" + id], name = props["name_" + id];
                var watts = isNum(w) ? parseFloat(w) : 0;
                total += watts;
                var s = sideOf(name);
                if (s) side[s] += watts; else unnamed++;
                rows += "<div class='defiantRow'><span>" + esc(name || "MPPT " + id) + "</span><span>" +
                    (isNum(w) ? watts.toFixed(0) + " W" : "--") + "</span></div>";
            });
            var compare;
            if (unnamed) {
                compare = "<div class='defiantRow defiantDim'><span>Port/Stbd: " + unnamed +
                    " MPPT" + (unnamed > 1 ? "s" : "") + " unnamed</span></div>";
            } else {
                var diff = side.port - side.stbd;
                var lead = Math.abs(diff) < 5 ? "even" :
                    (diff > 0 ? "port +" : "stbd +") + Math.abs(diff).toFixed(0) + " W";
                compare = "<div class='defiantRow defiantCompare'><span>Port " + side.port.toFixed(0) +
                    " / Stbd " + side.stbd.toFixed(0) + " W</span><span>" + lead + "</span></div>";
            }
            return "<div class='defiantSolar'><div class='defiantSolarTotal'>" + total.toFixed(0) + " W</div>" +
                compare + rows + "</div>";
        }
    }, {caption: true});

    var DCDC = SK + "electrical.alternator.291.";
    avnav.api.registerWidget({
        name: "DefiantDcDc",
        caption: "DC/DC",
        storeKeys: {dcPower: DCDC + "power", dcMode: DCDC + "chargingMode"},
        renderHtml: function (props) {
            var mode = props.dcMode;
            if (!isNum(props.dcPower) && !mode) {
                return "<div class='widgetData defiantBig defiantDim'>--</div><div class='defiantSub defiantDim'>no data</div>";
            }
            var w = isNum(props.dcPower) ? parseFloat(props.dcPower) : 0;
            return "<div class='widgetData defiantBig'>" + w.toFixed(0) + " W</div>" +
                "<div class='defiantSub" + (mode === "off" ? " defiantDim" : "") + "'>" + esc(mode || "") + "</div>";
        }
    }, {caption: true});
})();
