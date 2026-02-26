/* AlJefra OS — Minimal site JavaScript */

/* Highlight current nav link */
document.addEventListener('DOMContentLoaded', function() {
    var page = window.location.pathname.split('/').pop() || 'index.html';
    var links = document.querySelectorAll('nav .nav-links a');
    links.forEach(function(link) {
        if (link.getAttribute('href') === page) {
            link.style.color = '#e6edf3';
        }
    });
});
