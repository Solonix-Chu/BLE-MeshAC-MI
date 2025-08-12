import Vue from 'vue'
import Router from 'vue-router'
import AC from './views/AC.vue'
const Login = () => import('./views/Login.vue')

Vue.use(Router)

const router = new Router({
  mode: 'history',
  base: process.env.BASE_URL,
  routes: [
    { path: '/login', name: 'login', component: Login },
    { path: '/ac', name: 'ac', component: AC },
    { path: '*', redirect: '/login' }
  ]
})

router.beforeEach((to, from, next) => {
  const authed = !!localStorage.getItem('auth')
  if (to.path === '/login') return next()
  if (!authed) return next('/login')
  next()
})

export default router
