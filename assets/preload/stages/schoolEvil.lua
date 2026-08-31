local bgpath = 'stages/school/schoolEVIL'

function onCreate()
    makeAnimatedLuaSprite('school', bgpath, 450, 524)
    addAnimationByPrefix('school', 'school', 'school', 0, false)
    setScrollFactor('school', 0.6, 0.9)
    setProperty('school.antialiasing', false)
    scaleObject('school', 6, 6)
    addLuaSprite('school', false)

    makeAnimatedLuaSprite('street', bgpath, 750, 660)
    addAnimationByPrefix('street', 'street', 'floor', 0, false)
    setScrollFactor('street', 1.0, 1.0);
    setProperty('street.antialiasing', false);
    scaleObject('street', 6, 6);
    addLuaSprite('street', false);

    setCameraShader("camGame", "wave", 2.0, 2.0, 2.0)
end
