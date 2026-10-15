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

    var MPPTS = ["288", "289", "290", "291"];
    var solarKeys = {};
    MPPTS.forEach(function (id) {
        solarKeys["mppt_" + id] = SK + "electrical.solar." + id + ".panelPower";
    });
    avnav.api.registerWidget({
        name: "DefiantSolar",
        caption: "Solar",
        storeKeys: solarKeys,
        renderHtml: function (props) {
            var total = 0, rows = "";
            MPPTS.forEach(function (id) {
                var w = props["mppt_" + id];
                if (isNum(w)) total += parseFloat(w);
                rows += "<div class='defiantRow'><span>MPPT " + id + "</span><span>" +
                    (isNum(w) ? parseFloat(w).toFixed(0) + " W" : "--") + "</span></div>";
            });
            return "<div class='defiantSolar'><div class='defiantSolarTotal'>" + total.toFixed(0) + " W</div>" + rows + "</div>";
        }
    }, {caption: true});
})();
