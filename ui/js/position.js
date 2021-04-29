
function positionToright() {
    screenWidth = window.innerWidth;
    let toLRSize = screenWidth - 192;
    
    let bar =  document.querySelector('.sidebar');
    let wrap = document.querySelector('.wrapper');
       
    bar.style.cssText = `
        left: ${toLRSize}px;
        transition: .3s;
        position: fixed;
    `;
    wrap.style.position = 'absolute';
    wrap.style.cssText = `
        left: 0;   
        transition: .3s; 
    `;
    document.querySelector('main').style.cssText = `
        margin-right: 174px;
        margin-left: 0px;
        transition: .3s;
    `;
    document.querySelector('header').style.cssText = `
        margin-right: 174px;
        margin-left: 0px;
        transition: .3s;
    `;
    document.querySelector('footer').style.cssText = `
        margin-right: 174px;
        margin-left: 0px;
        transition: .3s;
    `;
    bar.classList.remove('topBottom');
}

function positionToleft() {
    let bar =  document.querySelector('.sidebar');
    let wrap = document.querySelector('.wrapper');

    bar.style.cssText = `
        left: 0px;
        transition: .3s;
        position: fixed;
    `;
    wrap.style.cssText = `
        left: 0px;
        transition: .3s;
    `;
    document.querySelector('main').style.cssText = `
        margin-right: 0px;
        margin-left: 174px;
        transition: .3s;
    `;
    document.querySelector('header').style.cssText = `
        margin-right: 0px;
        margin-left: 174px;
        transition: .3s;
    `;
    document.querySelector('footer').style.cssText = `
        margin-right: 0px;
        margin-left: 174px;
        transition: .3s;
    `;
    bar.classList.remove('topBottom');
}



function positionTotop() {
    let bar =  document.querySelector('.sidebar');
    let wrap = document.querySelector('.wrapper');

    bar.style.cssText = `
        position: fixed;
        height: 75px;
        min-width: 100%;
        top: 0px;
        transition: .3s;
    `;
    wrap.style.cssText = `
        left: 0px;
        top: 75px;
        transition: .3s;
    `;
    document.querySelector('main').style.cssText = `
        margin-left: 0px;
        margin-right: 0px;
        transition: .3s;
    `;
    document.querySelector('header').style.cssText = `
        margin-left: 0px;
        margin-right: 0px;
        transition: .3s;
    `;
    document.querySelector('footer').style.cssText = `
        margin-left: 0px;
        margin-right: 0px;
        transition: .3s;
    `;
    bar.classList.add('topBottom');
}

function positionTobottom() {
    let screenHeight = window.innerHeight;
    let toTBSize = screenHeight - 75;
    
    let bar =  document.querySelector('.sidebar');
    let wrap = document.querySelector('.wrapper');

    bar.style.cssText = `
        position: fixed;
        height: 75px;
        min-width: 100%;
        top: ${toTBSize}px;
        transition: .3s;
    `;
    wrap.style.cssText = `
        left: 0px;
        top: 0px;
        transition: .3s;
    `;
    document.querySelector('main').style.cssText = `
        margin-left: 0px;
        margin-right: 0px;
        transition: .3s;
    `;
    document.querySelector('header').style.cssText = `
        margin-left: 0px;
        margin-right: 0px;
        transition: .3s;
    `;
    document.querySelector('footer').style.cssText = `
        margin-left: 0px;
        margin-right: 0px;
        transition: .3s;
    `;
    bar.classList.add('topBottom');
}