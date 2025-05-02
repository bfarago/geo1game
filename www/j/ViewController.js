export class ViewController {
    constructor(globView, terrainView) {
        this.views = {
            globe: globView,
            terrain: terrainView
        };
        this.currentView = null;
        this.options = window.CONFIG['options'];
        this.crosshair = {
            "lat" : CONFIG['last_known_location'][0],
            lon : CONFIG['last_known_location'][1],
            alt : CONFIG['last_known_location'][2]
        }
        this.onOptionChangeCallback = null;
    }

    switchView(newView) {
        if (this.currentView) {
            this.currentView.hide();
        }
        this.currentView = newView;
        if (this.currentView) {
            this.currentView.init();  // biztos ami biztos
            this.currentView.show();
            console.log("[ViewController] Switched to view:", this.currentView.name);
        }
    }

    getCurrentView() {
        return this.currentView;
    }

    getViewByName(name) {
        return this.views[name];
    }

    setCrosshairUpdateCallback(callback) {
        for (const v of Object.values(this.views)) {
            if (v.setCrosshairUpdateCallback) v.setCrosshairUpdateCallback(callback);
        }
    }

    setOnRegionHover(callback) {
        for (const v of Object.values(this.views)) {
            if (v.setOnRegionHover) v.setOnRegionHover(callback);
        }
    }

    setOnRegionClick(callback) {
        for (const v of Object.values(this.views)) {
            if (v.setOnRegionClick) v.setOnRegionClick(callback);
        }
    }
    notifyCrosshairChange() {
        for (const v of Object.values(this.views)) {
            if (v.updateCrosshair) v.updateCrosshair(this.crosshair.lat, this.crosshair.lon, this.crosshair.alt );
        }
    }
    setCrosshair(lat, lon, alt) {
        if (lon > 180) lon -= 360;
        if (lon < -180) lon += 360;
        if (lat > 90) lat -= 180;
        if (lat < -90) lat += 180;
        // here we can compare, if we need an onchange event.
        this.crosshair = {lat, lon, alt};
        this.notifyCrosshairChange();
    }
    getCrosshair() {
        return this.crosshair;
    }

    setOnOptionChangeCallback(callback) {
        this.onOptionChangeCallback = callback;
    }
    notifyOptionChange(optionId, value, previous) {
        for (const v of Object.values(this.views)) {
            if (v.setOption) v.setOption(optionId, value, previous);
        }
        if (this.onOptionChangeCallback){
            this.onOptionChangeCallback(optionId, value, previous); 
        }
    }
    refreshAllUI() {
        console.log("Refresh all UI")
        for (const optionId in this.options) {
            this.setOption(optionId, this.options[optionId])
        }
        if (this.currentView?.hide) {
            this.currentView.hide();
        }
        if (this.currentView?.show) {
            this.currentView.show();
        }
    }
    getOption(optionId){
        return this.options[optionId];
    }
    setOption(optionId, value) {
        const previous = this.options[optionId];
        this.options[optionId] = value;
        this.notifyOptionChange(optionId, value, previous)
    }
    toggleOption(optionId) {
        const newvalue=!this.options[optionId];
        this.setOption(optionId, newvalue);
    }
}