// The panel switch. The page already answers the browser's own setting in CSS; this only adds
    // the manual patch, so it is built here and stays absent when JavaScript is off.
    (function () {
      var host = document.querySelector(".panel-switch");
      if (!host) return;
      var panels = [{ id: "beige", label: "beige" }, { id: "copper", label: "copper" }];
      var dark = window.matchMedia("(prefers-color-scheme: dark)");

      function current() {
        var pinned = document.documentElement.dataset.theme;
        if (pinned === "beige" || pinned === "copper") return pinned;
        return dark.matches ? "copper" : "beige";
      }
      function paint() {
        var now = current();
        host.querySelectorAll("button").forEach(function (button) {
          button.setAttribute("aria-checked", String(button.dataset.panel === now));
          button.tabIndex = button.dataset.panel === now ? 0 : -1;
        });
      }
      function patch(panel) {
        document.documentElement.dataset.theme = panel;
        try { localStorage.setItem("relay-panel", panel); } catch (e) {}
        paint();
      }

      panels.forEach(function (panel) {
        var button = document.createElement("button");
        button.type = "button";
        button.setAttribute("role", "radio");
        button.dataset.panel = panel.id;
        button.textContent = panel.label;
        button.addEventListener("click", function () { patch(panel.id); });
        host.appendChild(button);
      });
      host.addEventListener("keydown", function (event) {
        if (["ArrowLeft", "ArrowUp", "ArrowRight", "ArrowDown"].indexOf(event.key) === -1) return;
        event.preventDefault();
        var next = current() === "beige" ? "copper" : "beige";
        patch(next);
        host.querySelector('[data-panel="' + next + '"]').focus();
      });
      // An unpinned page follows the browser, including a change made while it is open.
      dark.addEventListener("change", paint);
      host.hidden = false;
      paint();
    })();


    // One interactive moment on the page: re-patching the line. Everything works without it —
    // the shell jack is plugged in the markup, and its session is the one shown.
    (function () {
      var board = document.querySelector('.board');
      if (!board) return;
      var screen = document.querySelector('.screen');
      var text = screen.querySelector('.input-text');
      var chip = screen.querySelector('.dest-chip');
      var lines = { shell: 'git status --short', agent: 'why does the build fail?' };
      var labels = { shell: 'terminal', agent: 'agent' };
      function patch(target) {
        board.dataset.line = target;
        board.querySelectorAll('.jack').forEach(function (jack) {
          jack.setAttribute('aria-checked', String(jack.dataset.target === target));
          jack.tabIndex = jack.dataset.target === target ? 0 : -1;
        });
        screen.querySelectorAll('[data-for]').forEach(function (body) {
          body.hidden = body.dataset.for !== target;
        });
        text.textContent = lines[target];
        chip.textContent = labels[target];
        chip.className = 'chip dest-chip ' + (target === 'shell' ? 'sh-chip' : 'ag-chip');
      }
      patch(board.dataset.line);
      board.querySelectorAll('.jack').forEach(function (jack) {
        jack.addEventListener('click', function () { patch(jack.dataset.target); });
      });
      board.querySelector('.jacks').addEventListener('keydown', function (event) {
        if (['ArrowLeft', 'ArrowUp', 'ArrowRight', 'ArrowDown'].indexOf(event.key) === -1) return;
        event.preventDefault();
        var next = board.dataset.line === 'shell' ? 'agent' : 'shell';
        patch(next);
        board.querySelector('.jack-' + next).focus();
      });
    })();
