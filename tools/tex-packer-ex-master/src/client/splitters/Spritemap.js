import Splitter from './Splitter';

class Spritemap extends Splitter {
    static check(data, cb) {
        try {
            let json = JSON.parse(data);
            cb(json && json.ATLAS && Array.isArray(json.ATLAS.SPRITES));
        }
        catch(e) {
            cb(false);
        }
    }
    
    static split(data, options, cb) {
        let res = [];

        try {
            let json = JSON.parse(data);
            let sprites = json.ATLAS.SPRITES;
            
            for(let wrapper of sprites) {
                let item = wrapper.SPRITE;
                if (!item) continue;
                
                let x = item.x || 0;
                let y = item.y || 0;
                let w = item.w || 0;
                let h = item.h || 0;
                let offX = item.offX || 0;
                let offY = item.offY || 0;
                let sourceW = item.sourceW || w;
                let sourceH = item.sourceH || h;
                
                let trimmed = offX !== 0 || offY !== 0 || w < sourceW || h < sourceH;

                res.push({
                    name: Splitter.fixFileName(item.name),
                    frame: { x, y, w, h },
                    spriteSourceSize: {
                        x: offX,
                        y: offY,
                        w: w,
                        h: h
                    },
                    sourceSize: {
                        w: sourceW,
                        h: sourceH
                    },
                    rotated: false,
                    trimmed: trimmed
                });
            }
        }
        catch(e) {
            console.error(e);
        }

        cb(res);
    }

    static get type() {
        return 'Spritemap (Adobe Animate)';
    }
}

export default Spritemap;
