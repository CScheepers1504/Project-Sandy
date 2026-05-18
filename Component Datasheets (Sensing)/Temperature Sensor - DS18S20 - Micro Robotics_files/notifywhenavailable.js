//NotifyWhenAvailable
var clickedButtons;
var lastProductId = null;
var lastQuantity = null;
var origAddToCart = window.addToCart;
var nwaAddToCart;
var nwaAddToCartSelector = 'button#button-cart[onclick], [onclick^="cart.add"], [onclick^="addToCart"], [onclick^="mgk_cart.add"], [onclick^="ee_cart.add"], [onclick^="cart_theme.add"], .button[data-clk]';

nwaAddToCart = function(product_id, quantity) {
    $('.alert, .text-danger').remove();
    $('.form-group').removeClass('has-error');
    $('#cart > button').button('reset');

    $('#NotifyWhenAvailable_popup').detach().appendTo('body');
    var btn = clickedButtons;

    offset = $(btn).offset();

    $('div#NotifyWhenAvailable_popup').fadeIn('slow');
    $(".NWA_popover-content").load("index.php?route=extension/module/notifywhenavailable/shownotifywhenavailableform&product_id="+product_id, function() {
        var window_width = $(window).width();
        var window_height = $(window).height();

        if(window_width >= 600 && window_width <= 1024) {

            if($('.modal-backdrop').length < 1) {
                $('<div class="modal-backdrop fade in"></div>').appendTo(document.body);
            } else {
                $('.modal-backdrop').css('display','block');
            }

            $('body').css('overflow-y', 'hidden');

            $('.NWA_popover.bottom .arrow').css('display','none');

            $('div#NotifyWhenAvailable_popup').css({
                top: '50%',
                left: '50%',
            position: 'fixed',
            'margin-top': -$('div.nwa-inner-wrapper').height()/2,
            'margin-left': -$('div#NotifyWhenAvailable_popup').width()/2,

            });
        }
        else if (window_width < 600) {
            if($('.modal-backdrop').length < 1) {
                $('<div class="modal-backdrop fade in"></div>').appendTo(document.body);
            } else {
                $('.modal-backdrop').css('display','block');
            }

            $('body').css('overflow-y', 'hidden');

            $('.NWA_popover.bottom .arrow').css('display','none');

			var width = $('div#NotifyWhenAvailable_popup').width(), leftSide = ($(window).width() / 2) - (width / 2);

            $('div#NotifyWhenAvailable_popup').css({
                top: 'calc(50% - 50%)',
                left: leftSide - 10,
                position: 'fixed',
                // 'margin-top': -$('.nwa-inner-wrapper').height()/2,
                // 'margin-left': -$('.nwa-inner-wrapper').width()/2,
            });
        } else {
        $('div#NotifyWhenAvailable_popup').css({
            top: offset.top,
            left: ((offset.left-$('div#NotifyWhenAvailable_popup').width()/2) + $('#button-cart').width()/2)
          });
        }
    });
};

function nwa(e, product_id, quantity) {
    e.preventDefault();
    e.stopPropagation();
    e.stopImmediatePropagation();
    clickedButtons = e.target;
    nwaAddToCart(product_id, quantity);
}

var checkQuantityNWA = function(product_ids) {

    $.ajax({
        url: 'index.php?route=extension/module/notifywhenavailable/checkQuantityNWA',
        type: 'post',
        data: {product_ids: product_ids},
        dataType: 'json',
        success: function(json) {
            if(json) {
                $(nwaAddToCartSelector).each(function(i,e){

                		var rpAttr = $(this).attr('onclick');
                		var params = null;
				    	if (typeof rpAttr === 'undefined' || rpAttr === false) {
                            if ($(this).attr('data-clk')) {
                                var params = $(this).attr('data-clk').match(/\d+/);
                            } else {
    				    		params = [$('input[type="hidden"][name="product_id"]').val()];
                            }
				    	} else {
	                        params = $(this).attr('onclick').match(/\d+/);
	                    }

                        var rpAttr = $(this).attr('onclick');
                        if (typeof rpAttr === 'undefined' || rpAttr === false) {
                            params = [$('input[type="hidden"][name="product_id"]').val()];
                        }
                        var params = $(this).attr('onclick').match(/\d+/);
                        var func_call = 'nwa(event, ';
                        var button = $(this);
                        if (params) {
                            if (params[0]) {
                                func_call += params[0];
                                if (params[1]) {
                                    func_call += ', ' + params[1];
                                } else {
                                    func_call += ', 1';
                                }
                            }
                        }
                        func_call += ');';
                        for (i in json) {
                            if(!json[i].PO && json[i].product_id == params[0]) {
                                button.attr('onclick', func_call);
                                button.find('span').html(json[i]['NWA_text']);
                                button.attr('data-hint',json[i]['NWA_text']);
                                button.attr('data-original-title', 'Notify Me!');
                                button.prop('disabled', false);
                                button.addClass('NWA-btn-action');
                                if(button.parents('.cart').hasClass('outofstock')) {
                                    button.parents('.cart').removeClass('outofstock');
                                }
                            }
                        }
                    });
            }
        }
    });

};

var rescanPage = function(old_product_ids) {
    var check_product_ids = [];
    if ($('input[type="hidden"][name="product_id"]').length == 1) {
        check_product_ids.push($('input[type="hidden"][name="product_id"]').val());
    }
    $(nwaAddToCartSelector).each(function(i,e){
        var rpAttr = $(this).attr('onclick');
        if (typeof rpAttr === 'undefined' || rpAttr === false) {
            return;
        }
        var params = $(this).attr('onclick').match(/\d+/);
        check_product_ids.push(params[0]);
    });
    checkQuantityNWA(check_product_ids);
}

$(document).ready(function() {
    var check_product_ids = [];
    $(nwaAddToCartSelector).each(function(i,e){
    	var rpAttr = $(this).attr('onclick');
    	if (typeof rpAttr === 'undefined' || rpAttr === false) {
    		return;
    	}
        if ($(this).attr('data-clk')) {
            var params = $(this).attr('data-clk').match(/\d+/);
        } else {
            var params = $(this).attr('onclick').match(/\d+/);
        }

        var rpAttr = $(this).attr('onclick');
        if (typeof rpAttr === 'undefined' || rpAttr === false) {
            return;
        }
        var params = $(this).attr('onclick').match(/\d+/);
        check_product_ids.push(params[0]);
    });

    checkQuantityNWA(check_product_ids);

    $(document).click(function(event) {
        if (!$(event.target).is("#NotifyWhenAvailable_popup,  #button-nwa-duplicate, #button-nwa-duplicate span, button[lkmwa=true], input[lkmwa=true], .NWA_popover-title, .arrow, .NWA_popover-content, #NWAYourName, #NWAYourEmail, #NWAYourComment, #NWAYourComment, #NWACaptchaImage, #NWACaptcha, #NotifyWhenAvailableSubmit, #NotifyWhenAvailable_popup p, #NotifyWhenAvailable_popup span, .NWA_popover, #NotifyWhenAvailableForm, .NWAError, input.NWA_popover_field_error, #button-cart, #NWAPrivacyPolicy, #NWAPrivacyPolicy *")) {
            $('div#NotifyWhenAvailable_popup').fadeOut(300);
            $('.modal-backdrop').fadeOut(300);
            $('body').css('overflow-y', 'inherit');
        }
    });

    var rescanTimer = null;
    $(document).bind('DOMNodeInserted', function(event) {
        let insertedEl = $(event.target);
        if (!insertedEl.is(nwaAddToCartSelector) && insertedEl.find(nwaAddToCartSelector).length == 0) return;
        if (rescanTimer != null) {
            clearTimeout(rescanTimer);
        }
        rescanTimer = setTimeout(rescanPage.bind(null, check_product_ids), 1000);
    });
});