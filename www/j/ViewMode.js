export class ViewMode {
    constructor(name) {
        this.name = name; 
        this.biomTexture=null;
        this.elevTexture=null;
        this.cloudTexture=null;
        this.starState=null;
        this.regions=null;
        this.selectedRegion=null;
        this.selectedRegion2=null; //probably double click?
        this.hoveredRegion=null;
    }
    init() {}
    update(delta) {}
    updateCrosshair(lat, lon, alt) {}
    updateUserMarkers() {}
    updateRegionLights() {}
    setCrosshairUpdateCallback(callback) {
        this.crosshairCallback = callback;
    }
    setOnRegionHover(callback) {
        this.onRegionHover = callback;
    }
    setOnRegionClick(callback) {
        this.onRegionClick = callback;
    }
    setOnRegionDoubleClick(callback) {
        this.onRegionDoubleClick = callback;
    }
    handleHover(raycaster) {}
    handleClick(raycaster) {}
    handleDoubleClick(raycaster) {}
    show() {}
    hide() {}
    dispose() {}
    toggleGraticule() {}
    setBiomTexture(texture) {
        this.biomTexture=texture;
    }
    setElevTexture(texture) {
        this.elevTexture=texture;
    }
    setCloudTexture(texture) {
        this.cloudTexture=texture;
    }
    setStarState(state){
        this.starState=state;
    }
    setRegions(regions) {
        this.regions=regions;
    }
    latLonAltToScreen(lat, lon, alt) { return null; }
    screenToLatLonAlt(x, y) { return null; }
    screenToWorldPosition(x, y) { return null; }
    latLonAltToWorldPosition(lat, lon, alt) { return null; }
}